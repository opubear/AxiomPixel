#include "Formula/Process/Runtime.h"
#include "../ProcessProgram.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pixel::formula::process {
namespace {
struct RuntimeFault { SourceSpan Source; std::string Message; };
[[noreturn]] void Fault(SourceSpan source, std::string message) { throw RuntimeFault{source, std::move(message)}; }
bool ValidPosition(const Position &position) {
    return std::isfinite(position.OffsetX) && std::isfinite(position.OffsetY)
        && position.OffsetX >= 0 && position.OffsetX < 1 && position.OffsetY >= 0 && position.OffsetY < 1;
}
bool ValidLimits(ExecutionLimits limits) {
    return limits.Work > 0 && limits.Work <= 4096 && limits.Queries <= 32 && limits.Outputs <= 64;
}
bool ValidOutcome(Outcome outcome) { return outcome >= Outcome::None && outcome <= Outcome::Timeout; }
bool ValidField(const FieldSource &source) {
    return std::isfinite(source.Radius) && source.Radius >= 0
        && std::isfinite(source.AccelerationX) && std::isfinite(source.AccelerationY)
        && source.Lifetime > 0 && source.Lifetime <= std::uint64_t(INT64_MAX);
}
bool ValidEventHandle(const HostEvent &event) {
    return event.Handle.Id == 0 || (event.Result == Outcome::Applied
        && event.Request.Sequence != 0 && event.WakeSubscription.Sequence == 0);
}
bool ValidValue(const Value &value) {
    const auto nonnegative = [](double number) { return std::isfinite(number) && number >= 0; };
    const auto duration = [](std::uint64_t ticks) { return ticks > 0 && ticks <= std::uint64_t(INT64_MAX); };
    switch (value.index()) {
    case 0: case 1: return true;
    case 2: return std::isfinite(std::get<double>(value));
    case 3: return nonnegative(std::get<Cells>(value).Value);
    case 4: return std::isfinite(std::get<Speed>(value).Value);
    case 5: return duration(std::get<Duration>(value).Value);
    case 6: {
        const auto &effect = std::get<Effect>(value);
        if (const auto *dig = std::get_if<Dig>(&effect)) return nonnegative(dig->Radius);
        if (const auto *wind = std::get_if<Wind>(&effect))
            return nonnegative(wind->Radius) && std::isfinite(wind->VelocityX) && std::isfinite(wind->VelocityY);
        if (const auto *bolt = std::get_if<Bolt>(&effect))
            return nonnegative(bolt->Damage) && std::isfinite(bolt->VelocityX) && duration(bolt->Lifetime);
        if (const auto *field = std::get_if<FieldSource>(&effect)) return ValidField(*field);
        if (const auto *update = std::get_if<UpdateField>(&effect)) return ValidField(update->Source);
        return std::holds_alternative<StopField>(effect);
    }
    case 7: return ValidOutcome(std::get<Outcome>(value));
    case 8: return std::isfinite(std::get<Acceleration>(value).Value);
    case 9: return true; // Zero is an empty handle; ownership belongs to the host.
    }
    return false;
}
double Number(const Value &value) {
    if (const auto *integer = std::get_if<std::int64_t>(&value)) return static_cast<double>(*integer);
    return std::get<double>(value);
}
double Finite(double value, SourceSpan source) {
    if (!std::isfinite(value)) Fault(source, "numeric result is not finite");
    return value;
}
double Nonnegative(double value, SourceSpan source) {
    if (Finite(value, source) < 0) Fault(source, "effect parameter must be nonnegative");
    return value;
}
std::int64_t AddInteger(std::int64_t left, std::int64_t right, SourceSpan source) {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right)
        || (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
        Fault(source, "integer overflow");
    return left + right;
}
} // namespace

struct Instance::Execution {
    Execution(ProgramArtifact program, ExecutionLimits limits) : Program(std::move(program)), Limits(limits) {
        if (Program.Identity().SemanticsVersion == 2) OriginScopes.reserve(24);
    }

    ProgramArtifact Program;
    InstanceState State;
    ExecutionLimits Limits;
    std::array<Value, 16> Locals{};
    std::optional<Boundary> LastBoundary;
    std::uint64_t EventSequence = 0, SubscriptionSequence = 0;
    AwaitKind WaitingFor = AwaitKind::Result;
    std::optional<HostEvent> Receipt, Impact, Wake;
    bool ReceiptSeen = false, ImpactSeen = false;
    bool OriginPinned = false;
    std::vector<InstanceSnapshot::OriginScope> OriginScopes;

    std::string ValidateSnapshot(const InstanceSnapshot &snapshot) const {
        const bool composition = Program.Identity().SemanticsVersion == 2;
        if (snapshot.SchemaVersion != (composition ? 3u : 2u)) return "unsupported instance state version";
        if (snapshot.Program != Program.Identity()) return "instance state requires its exact program identity";
        const auto &state = snapshot.State;
        if (state.Id == 0 || !ValidLimits(snapshot.Limits) || !ValidPosition(state.Origin)
            || !ValidOutcome(state.LastOutcome) || state.ActiveSteps == 0
            || state.Remaining == 0 || state.Remaining > std::uint64_t(INT64_MAX)
            || (state.LastEffectHandle.Id != 0 && state.LastOutcome != Outcome::Applied))
            return "invalid instance state values";
        if ((state.Status != Phase::Sleeping && state.Status != Phase::Awaiting)
            || state.Instruction == 0 || state.Instruction >= Program.InstructionCount())
            return "instance state is not at a suspension point";
        const auto &yield = Program.Data->Instructions[state.Instruction - 1];
        if (snapshot.OriginScopes.size() != yield.LiveOriginScopes || snapshot.OriginScopes.size() > 24
            || snapshot.OriginPinned != yield.OriginPinned) return "invalid origin scope stack";
        for (std::size_t i = 0; i < snapshot.OriginScopes.size(); ++i) {
            const auto &scope = snapshot.OriginScopes[i];
            if (!ValidPosition(scope.Origin) || scope.Pinned != ((yield.PinnedOriginScopes & (1u << i)) != 0))
                return "invalid saved origin scope";
        }
        if ((state.Status == Phase::Sleeping && yield.Kind != InstructionKind::Wait)
            || (state.Status == Phase::Awaiting && (yield.Kind != InstructionKind::Await || snapshot.WaitingFor != yield.Await)))
            return "instance continuation does not match its suspension point";
        for (std::size_t slot = 0; slot < snapshot.Locals.size(); ++slot) {
            const auto &value = snapshot.Locals[slot];
            const bool live = (yield.LiveLocals & (1u << slot)) != 0;
            if (live != value.has_value() || (value && (slot >= Program.Data->LocalTypes.size()
                || value->index() != std::size_t(Program.Data->LocalTypes[slot]) || !ValidValue(*value))))
                return "instance local values do not match the compiled continuation";
        }

        const auto request = state.LastRequest;
        if (request.Instance != (request.Sequence ? state.Id : 0)
            || (state.Status == Phase::Awaiting && snapshot.WaitingFor != AwaitKind::Wake && request.Sequence == 0))
            return "invalid request identity in instance state";
        const bool awaitingWake = state.Status == Phase::Awaiting && snapshot.WaitingFor == AwaitKind::Wake;
        if (awaitingWake != state.WakeSubscription.has_value()
            || (state.WakeSubscription && (state.WakeSubscription->Instance != state.Id
                || state.WakeSubscription->Sequence == 0 || state.WakeSubscription->Sequence != snapshot.SubscriptionSequence)))
            return "invalid wake subscription in instance state";
        const auto validEvent = [&](const HostEvent &event) {
            return event.Target == state.Id && event.Sequence > 0 && event.Sequence <= snapshot.EventSequence
                && event.CommittedAt < snapshot.LastBoundary && ValidPosition(event.HitPosition) && ValidEventHandle(event);
        };
        const auto receipt = [](Outcome result) {
            return result == Outcome::Applied || result == Outcome::Rejected || result == Outcome::Cancelled;
        };
        const auto impact = [](Outcome result) {
            return result == Outcome::Hit || result == Outcome::Expired || result == Outcome::Rejected || result == Outcome::Cancelled;
        };
        if (snapshot.Receipt && (!snapshot.ReceiptSeen || !validEvent(*snapshot.Receipt) || request.Sequence == 0
            || snapshot.Receipt->Request != request || !receipt(snapshot.Receipt->Result)))
            return "invalid cached receipt";
        if (snapshot.Impact && (!snapshot.ImpactSeen || !validEvent(*snapshot.Impact) || request.Sequence == 0
            || snapshot.Impact->Request != request || !impact(snapshot.Impact->Result)))
            return "invalid cached impact";
        if (snapshot.Wake && (!validEvent(*snapshot.Wake) || !state.WakeSubscription
            || snapshot.Wake->WakeSubscription != *state.WakeSubscription
            || (snapshot.Wake->Result != Outcome::Wake && snapshot.Wake->Result != Outcome::Rejected
                && snapshot.Wake->Result != Outcome::Cancelled)))
            return "invalid cached wake";
        return {};
    }

    SourceSpan CurrentSource() const { return Program.Data->Instructions.at(State.Instruction).Source; }
    void Charge(StepResult &result, SourceSpan source) const {
        if (result.WorkUsed == Limits.Work) Fault(source, "work budget exceeded");
        ++result.WorkUsed;
    }
    Value Evaluate(std::size_t index, const HostView &view, StepResult &result) {
        const auto &expression = Program.Data->Expressions[index];
        const auto source = expression.Source;
        Charge(result, source);
        const auto argument = [&](std::size_t i) { return Evaluate(expression.Arguments[i], view, result); };
        switch (expression.Kind) {
        case ExpressionKind::Literal: return expression.Literal;
        case ExpressionKind::Local: return Locals[expression.Slot];
        case ExpressionKind::Outcome: return State.LastOutcome;
        case ExpressionKind::EffectHandle: return State.LastEffectHandle;
        case ExpressionKind::Inventory:
            if (result.QueriesUsed == Limits.Queries) Fault(source, "query budget exceeded");
            ++result.QueriesUsed;
            if (!view.Inventory) Fault(source, "inventory query unavailable in this snapshot");
            return *view.Inventory;
        case ExpressionKind::Add:
        case ExpressionKind::Less:
        case ExpressionKind::Equal: {
            const auto left = argument(0);
            const auto right = argument(1);
            if (expression.Kind == ExpressionKind::Add) {
                if (const auto *integer = std::get_if<std::int64_t>(&left))
                    return AddInteger(*integer, std::get<std::int64_t>(right), source);
                return Finite(std::get<double>(left) + std::get<double>(right), source);
            }
            if (expression.Kind == ExpressionKind::Less) {
                if (const auto *integer = std::get_if<std::int64_t>(&left)) return *integer < std::get<std::int64_t>(right);
                return std::get<double>(left) < std::get<double>(right);
            }
            if (const auto *integer = std::get_if<std::int64_t>(&left)) return *integer == std::get<std::int64_t>(right);
            if (const auto *number = std::get_if<double>(&left)) return *number == std::get<double>(right);
            if (const auto *boolean = std::get_if<bool>(&left)) return *boolean == std::get<bool>(right);
            return std::get<Outcome>(left) == std::get<Outcome>(right);
        }
        case ExpressionKind::Not: return !std::get<bool>(argument(0));
        case ExpressionKind::Negate: {
            const auto value = argument(0);
            if (const auto *integer = std::get_if<std::int64_t>(&value)) {
                if (*integer == std::numeric_limits<std::int64_t>::min()) Fault(source, "integer overflow");
                return -*integer;
            }
            return -std::get<double>(value);
        }
        case ExpressionKind::Cells: return Cells{Nonnegative(Number(argument(0)), source)};
        case ExpressionKind::Speed: return Speed{Number(argument(0))};
        case ExpressionKind::Acceleration: return Acceleration{Number(argument(0))};
        case ExpressionKind::Ticks: {
            const auto value = std::get<std::int64_t>(argument(0));
            if (value <= 0) Fault(source, "duration must be positive");
            return Duration{static_cast<std::uint64_t>(value)};
        }
        case ExpressionKind::Dig: return Effect{Dig{std::get<Cells>(argument(0)).Value}};
        case ExpressionKind::Wind: {
            const auto radius = std::get<Cells>(argument(0)).Value;
            const auto x = std::get<Speed>(argument(1)).Value;
            const auto y = std::get<Speed>(argument(2)).Value;
            return Effect{Wind{radius, x, y}};
        }
        case ExpressionKind::Bolt: {
            const auto damage = Nonnegative(Number(argument(0)), source);
            const auto speed = std::get<Speed>(argument(1)).Value;
            const auto lifetime = std::get<Duration>(argument(2)).Value;
            return Effect{Bolt{damage, speed, lifetime}};
        }
        case ExpressionKind::Field: {
            const auto radius = std::get<Cells>(argument(0)).Value;
            const auto x = std::get<Acceleration>(argument(1)).Value;
            const auto y = std::get<Acceleration>(argument(2)).Value;
            const auto lifetime = std::get<Duration>(argument(3)).Value;
            return Effect{FieldSource{radius, x, y, lifetime}};
        }
        case ExpressionKind::UpdateField: {
            const auto handle = std::get<EffectHandle>(argument(0));
            const auto effect = std::get<Effect>(argument(1));
            const auto *field = std::get_if<FieldSource>(&effect);
            if (!field) Fault(source, "update_field requires a field value");
            return Effect{UpdateField{handle, *field}};
        }
        case ExpressionKind::StopField: return Effect{StopField{std::get<EffectHandle>(argument(0))}};
        case ExpressionKind::DamageAdd:
        case ExpressionKind::DamageMultiply:
        case ExpressionKind::SpeedMultiply: {
            auto effect = std::get<Effect>(argument(0));
            const auto amount = Number(argument(1));
            auto *bolt = std::get_if<Bolt>(&effect);
            if (!bolt) Fault(source, "modifier requires a bolt value");
            if (expression.Kind == ExpressionKind::DamageAdd) bolt->Damage = Nonnegative(bolt->Damage + amount, source);
            else if (expression.Kind == ExpressionKind::DamageMultiply) bolt->Damage = Nonnegative(bolt->Damage * amount, source);
            else bolt->VelocityX = Finite(bolt->VelocityX * amount, source);
            return effect;
        }
        }
        throw std::logic_error("unhandled compiled expression");
    }

    void Ingest(const HostView &view, std::span<const HostEvent> events) {
        if (events.size() > 32) Fault(CurrentSource(), "event delivery budget exceeded");
        std::uint64_t previous = 0;
        for (const auto &event : events) {
            if (event.Target != State.Id) continue;
            if (event.Sequence <= previous || event.CommittedAt >= view.At)
                throw std::invalid_argument("events must be ordered and committed before the active boundary");
            if (!ValidOutcome(event.Result) || event.Result == Outcome::None || event.Result == Outcome::Timeout
                || !ValidPosition(event.HitPosition) || !ValidEventHandle(event))
                throw std::invalid_argument("invalid host event value");
            previous = event.Sequence;
        }
        for (const auto &event : events) {
            if (event.Target != State.Id || event.Sequence <= EventSequence) continue;
            EventSequence = event.Sequence;
            if (State.WakeSubscription && event.WakeSubscription == *State.WakeSubscription
                && (event.Result == Outcome::Wake || event.Result == Outcome::Rejected || event.Result == Outcome::Cancelled)) {
                if (!Wake) Wake = event;
            } else if (State.LastRequest.Sequence != 0 && event.Request == State.LastRequest) {
                // Keep early replies through ordinary waits. Separate caches let a
                // caller await the admission receipt and then the carrier terminal.
                if (event.Result == Outcome::Applied || event.Result == Outcome::Rejected || event.Result == Outcome::Cancelled) {
                    if (!ReceiptSeen) { Receipt = event; ReceiptSeen = true; }
                }
                if (event.Result == Outcome::Hit || event.Result == Outcome::Expired
                    || event.Result == Outcome::Rejected || event.Result == Outcome::Cancelled) {
                    if (!ImpactSeen) { Impact = event; ImpactSeen = true; }
                }
            }
        }
    }
    bool Resume() {
        if (State.Status == Phase::Awaiting) {
            auto &event = WaitingFor == AwaitKind::Result ? Receipt : WaitingFor == AwaitKind::Hit ? Impact : Wake;
            if (event) {
                State.LastOutcome = event->Result;
                State.LastEffectHandle = WaitingFor == AwaitKind::Result && event->Result == Outcome::Applied
                    ? event->Handle : EffectHandle{};
                if (event->Result == Outcome::Hit) State.Origin = event->HitPosition;
                event.reset();
            } else if (--State.Remaining != 0) return false;
            else {
                State.LastOutcome = Outcome::Timeout;
                State.LastEffectHandle = {};
            }
            State.WakeSubscription.reset();
            Wake.reset();
        } else if (State.Status == Phase::Sleeping && --State.Remaining != 0) return false;
        State.Remaining = 0;
        State.Status = Phase::Ready;
        return true;
    }
    void End(StepResult &result, EndReason reason) {
        State.Status = Phase::Ended;
        State.Remaining = 0;
        State.WakeSubscription.reset();
        Receipt.reset(); Impact.reset(); Wake.reset();
        result.End = Termination{State.Id, reason};
    }
    bool Execute(const Instruction &instruction, const HostView &view, StepResult &result) {
        Charge(result, instruction.Source);
        switch (instruction.Kind) {
        case InstructionKind::Assign:
            Locals[instruction.Slot] = Evaluate(instruction.Expression, view, result);
            ++State.Instruction;
            break;
        case InstructionKind::Emit: {
            auto effect = std::get<Effect>(Evaluate(instruction.Expression, view, result));
            if (result.Count == Limits.Outputs) Fault(instruction.Source, "output budget exceeded");
            if (State.LastRequest.Sequence == std::numeric_limits<std::uint64_t>::max()) Fault(instruction.Source, "request sequence exhausted");
            State.LastRequest = {State.Id, State.LastRequest.Sequence + 1};
            Receipt.reset(); Impact.reset();
            ReceiptSeen = false; ImpactSeen = false;
            result.Requests[result.Count++] = {State.LastRequest, State.Origin, std::move(effect), instruction.Source};
            ++State.Instruction;
            break;
        }
        case InstructionKind::Branch:
            State.Instruction = std::get<bool>(Evaluate(instruction.Expression, view, result))
                ? State.Instruction + 1 : instruction.Target;
            break;
        case InstructionKind::Jump: State.Instruction = instruction.Target; break;
        case InstructionKind::Wait:
        case InstructionKind::Await:
            State.Remaining = std::get<Duration>(Evaluate(instruction.Expression, view, result)).Value;
            if (instruction.Kind == InstructionKind::Await) {
                WaitingFor = instruction.Await;
                if (WaitingFor != AwaitKind::Wake && State.LastRequest.Sequence == 0)
                    Fault(instruction.Source, "await requires a previously emitted request");
                if (WaitingFor == AwaitKind::Wake) {
                    if (SubscriptionSequence == std::numeric_limits<std::uint64_t>::max()) Fault(instruction.Source, "subscription sequence exhausted");
                    State.WakeSubscription = Subscription{State.Id, ++SubscriptionSequence};
                }
            }
            State.Status = instruction.Kind == InstructionKind::Wait ? Phase::Sleeping : Phase::Awaiting;
            ++State.Instruction;
            return false;
        case InstructionKind::Stop: End(result, EndReason::Stopped); return false;
        case InstructionKind::Complete: End(result, EndReason::Completed); return false;
        case InstructionKind::ScopeBegin:
            OriginScopes.push_back({State.Origin, OriginPinned});
            ++State.Instruction;
            break;
        case InstructionKind::ScopeHit:
            OriginPinned = true;
            ++State.Instruction;
            break;
        case InstructionKind::ScopeEnd: {
            const auto parent = OriginScopes.back();
            OriginScopes.pop_back();
            OriginPinned = parent.Pinned;
            State.Origin = !OriginPinned && view.Origin ? *view.Origin : parent.Origin;
            ++State.Instruction;
            break;
        }
        }
        return true;
    }
};

Instance::Instance(std::unique_ptr<Execution> execution) : Run(std::move(execution)) {}
Instance::~Instance() = default;
const ProgramArtifact &Instance::Program() const { return Run->Program; }
const InstanceState &Instance::State() const { return Run->State; }
bool Instance::HasPinnedOrigin() const { return Run->OriginPinned; }

InstanceSnapshot Instance::ExportState() const {
    const auto &state = Run->State;
    if (state.Status != Phase::Sleeping && state.Status != Phase::Awaiting)
        throw std::logic_error("export requires a yielded process and a committed host slice");
    InstanceSnapshot snapshot;
    snapshot.SchemaVersion = Run->Program.Identity().SemanticsVersion == 2 ? 3 : 2;
    snapshot.Program = Run->Program.Identity();
    snapshot.State = state;
    snapshot.Limits = Run->Limits;
    const auto live = Run->Program.Data->Instructions.at(state.Instruction - 1).LiveLocals;
    for (std::size_t slot = 0; slot < Run->Locals.size(); ++slot)
        if (live & (1u << slot)) snapshot.Locals[slot] = Run->Locals[slot];
    snapshot.LastBoundary = Run->LastBoundary.value();
    snapshot.EventSequence = Run->EventSequence;
    snapshot.SubscriptionSequence = Run->SubscriptionSequence;
    snapshot.WaitingFor = Run->WaitingFor;
    snapshot.Receipt = Run->Receipt;
    snapshot.Impact = Run->Impact;
    snapshot.Wake = Run->Wake;
    snapshot.ReceiptSeen = Run->ReceiptSeen;
    snapshot.ImpactSeen = Run->ImpactSeen;
    snapshot.OriginPinned = Run->OriginPinned;
    snapshot.OriginScopes = Run->OriginScopes;
    return snapshot;
}

std::variant<std::unique_ptr<Instance>, Diagnostic> Instance::Restore(
    ProgramArtifact program, const InstanceSnapshot &snapshot, Capabilities granted) {
    auto execution = std::make_unique<Execution>(std::move(program), snapshot.Limits);
    if (const auto error = execution->ValidateSnapshot(snapshot); !error.empty())
        return Diagnostic{execution->Program.Identity().Resource, {}, error};
    if ((execution->Program.RequiredCapabilities() & granted) != execution->Program.RequiredCapabilities())
        return Diagnostic{execution->Program.Identity().Resource, {}, "host refused required capabilities on restore"};

    execution->State = snapshot.State;
    for (std::size_t slot = 0; slot < snapshot.Locals.size(); ++slot)
        if (snapshot.Locals[slot]) execution->Locals[slot] = *snapshot.Locals[slot];
    execution->LastBoundary = snapshot.LastBoundary;
    execution->EventSequence = snapshot.EventSequence;
    execution->SubscriptionSequence = snapshot.SubscriptionSequence;
    execution->WaitingFor = snapshot.WaitingFor;
    execution->Receipt = snapshot.Receipt;
    execution->Impact = snapshot.Impact;
    execution->Wake = snapshot.Wake;
    execution->ReceiptSeen = snapshot.ReceiptSeen;
    execution->ImpactSeen = snapshot.ImpactSeen;
    execution->OriginPinned = snapshot.OriginPinned;
    execution->OriginScopes = snapshot.OriginScopes;
    return std::unique_ptr<Instance>(new Instance(std::move(execution)));
}

std::variant<std::unique_ptr<Instance>, Diagnostic> Instance::Start(
    ProgramArtifact program, InstanceId id, Capabilities granted, ExecutionLimits limits, Position origin) {
    if (id == 0 || !ValidLimits(limits) || !ValidPosition(origin))
        throw std::invalid_argument("invalid process instance configuration");
    if ((program.RequiredCapabilities() & granted) != program.RequiredCapabilities())
        return Diagnostic{program.Identity().Resource, {}, "host refused required capabilities"};
    auto execution = std::make_unique<Execution>(std::move(program), limits);
    execution->State.Id = id;
    execution->State.Origin = origin;
    return std::unique_ptr<Instance>(new Instance(std::move(execution)));
}
StepResult Instance::Step(const HostView &view, std::span<const HostEvent> events) {
    StepResult result;
    if (Run->State.Status == Phase::Ended) return result;
    if (Run->LastBoundary && view.At <= *Run->LastBoundary)
        throw std::invalid_argument("active boundaries must strictly increase");
    if (view.Origin && !ValidPosition(*view.Origin)) throw std::invalid_argument("invalid binding origin");
    if (view.Origin && !Run->OriginPinned) Run->State.Origin = *view.Origin;
    Run->LastBoundary = view.At;
    try {
        if (Run->State.ActiveSteps == std::numeric_limits<std::uint64_t>::max()) Fault(Run->CurrentSource(), "active clock exhausted");
        ++Run->State.ActiveSteps;
        Run->Ingest(view, events);
        if (!Run->Resume()) return result;
        while (Run->Execute(Run->Program.Data->Instructions.at(Run->State.Instruction), view, result)) {}
    } catch (const RuntimeFault &fault) {
        result.Count = 0;
        result.Fault = Diagnostic{Run->Program.Identity().Resource, fault.Source, fault.Message};
        Run->End(result, EndReason::Faulted);
    }
    return result;
}
StepResult Instance::Cancel() {
    StepResult result;
    if (Run->State.Status != Phase::Ended) Run->End(result, EndReason::Cancelled);
    return result;
}
} // namespace pixel::formula::process
