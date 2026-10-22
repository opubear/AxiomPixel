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
    Execution(ProgramArtifact program, ExecutionLimits limits) : Program(std::move(program)), Limits(limits) {}

    ProgramArtifact Program;
    InstanceState State;
    ExecutionLimits Limits;
    std::array<Value, 16> Locals{};
    std::optional<Boundary> LastBoundary;
    std::uint64_t EventSequence = 0, SubscriptionSequence = 0;
    AwaitKind WaitingFor = AwaitKind::Result;
    std::optional<HostEvent> Receipt, Impact, Wake;
    bool ReceiptSeen = false, ImpactSeen = false;

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
            if (event.Result == Outcome::None || event.Result == Outcome::Timeout || !ValidPosition(event.HitPosition))
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
                if (event->Result == Outcome::Hit) State.Origin = event->HitPosition;
                event.reset();
            } else if (--State.Remaining != 0) return false;
            else State.LastOutcome = Outcome::Timeout;
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
            result.Requests[result.Count++] = {State.LastRequest, State.Origin, std::move(effect)};
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
        }
        return true;
    }
};

Instance::Instance(std::unique_ptr<Execution> execution) : Run(std::move(execution)) {}
Instance::~Instance() = default;
const ProgramArtifact &Instance::Program() const { return Run->Program; }
const InstanceState &Instance::State() const { return Run->State; }

std::variant<std::unique_ptr<Instance>, Diagnostic> Instance::Start(
    ProgramArtifact program, InstanceId id, Capabilities granted, ExecutionLimits limits, Position origin) {
    if (id == 0 || limits.Work == 0 || limits.Work > 4096 || limits.Queries > 32 || limits.Outputs > 64 || !ValidPosition(origin))
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
