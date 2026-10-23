#include "ProcessHost.h"
#include "CheckpointCodec.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <tuple>

namespace pixel {
namespace {
namespace process = formula::process;
constexpr size_t MaximumReceipts = 256, MaximumEffects = 256;

process::Position LanguagePosition(WorldPosition position) {
    position.Normalize();
    const auto cell = position.Cell();
    const auto offset = position.RelativeTo(cell);
    return {cell.X, cell.Y, offset[0], offset[1]};
}
WorldPosition WorldPoint(process::Position position) {
    const CellCoord cell{position.CellX, position.CellY};
    const auto chunk = ChunkOf(cell);
    return {chunk, {double(cell.X - chunk.X * storage::ChunkEdge) + position.OffsetX,
                    double(cell.Y - chunk.Y * storage::ChunkEdge) + position.OffsetY}};
}
api::Position PublicPosition(process::Position position) {
    return {{position.CellX, position.CellY}, {position.OffsetX, position.OffsetY}};
}
bool ContainsDisk(WorldRect scope, WorldPosition position, double radius) {
    const auto offset = position.RelativeTo({scope.X, scope.Y});
    return offset[0] - radius >= 0 && offset[1] - radius >= 0
        && offset[0] + radius < scope.Width && offset[1] + radius < scope.Height;
}
double ScopeExit(WorldRect scope, WorldPosition from, WorldPosition to) {
    const auto start = from.RelativeTo({scope.X, scope.Y});
    const auto end = to.RelativeTo({scope.X, scope.Y});
    const double extent[]{double(scope.Width), double(scope.Height)};
    double first = 1;
    for (size_t axis = 0; axis < 2; ++axis) {
        const double delta = end[axis] - start[axis];
        const double radius = FormulaWorldAdapter::BoltRadius;
        if (delta > 0 && end[axis] + radius >= extent[axis])
            first = std::min(first, (extent[axis] - radius - start[axis]) / delta);
        if (delta < 0 && end[axis] - radius < 0)
            first = std::min(first, (radius - start[axis]) / delta);
    }
    return std::clamp(first, 0.0, 1.0);
}
formula::Vec2 Velocity(const ProcessCarrier &carrier, double speed) {
    const auto angle = std::remainder(carrier.AngleDegrees, 360.0) * std::numbers::pi / 180;
    return {speed * std::cos(angle), speed * std::sin(angle)};
}
process::Capability CapabilityOf(const process::Effect &effect) {
    if (std::holds_alternative<process::Dig>(effect)) return process::Capability::Dig;
    if (std::holds_alternative<process::Wind>(effect)) return process::Capability::Wind;
    if (std::holds_alternative<process::Bolt>(effect)) return process::Capability::Bolt;
    return process::Capability::Field;
}
struct Running {
    std::unique_ptr<process::Instance> Instance;
    ProcessCarrier Carrier;
    uint64_t Version = 0, Resource = 2, NextEvent = 1;
    std::optional<api::RequestKey> Activation;
    std::vector<process::HostEvent> Events;
    process::StepResult Slice;
    // Birth overlaps are also captured before any same-batch world edits.
    std::array<FormulaContact, 64> BirthContacts;
    bool Prepared = false, Suspended = false;
};
struct Projectile {
    process::RequestId Request;
    WorldPosition Position;
    formula::Vec2 Velocity;
    double Damage;
    uint64_t Remaining;
};
struct Collision {
    process::RequestId Request;
    FormulaContact Contact;
};
bool Observable(const Running &root, const process::HostEvent &event) {
    const auto &state = root.Instance->State();
    return event.Request.Sequence ? event.Request == state.LastRequest
        : state.WakeSubscription && event.WakeSubscription == *state.WakeSubscription;
}
} // namespace

struct ProcessHost::State {
    process::Boundary At = 0;
    std::vector<std::unique_ptr<Running>> Roots;
    std::vector<Projectile> Bolts;
    std::vector<Collision> Collisions;
    std::vector<FormulaEffectRecord> Effects;
    std::deque<ProcessReceipt> Receipts;
    std::deque<FormulaRuntimeDiagnostic> Diagnostics;
    ProcessHostStats Counters;
    size_t WindCallsThisFrame = 0;

    State() {
        Roots.reserve(MaximumInstances);
        Bolts.reserve(MaximumProjectiles);
        Collisions.reserve(MaximumProjectiles);
        Effects.reserve(MaximumEffects);
    }
    void Begin() {
        if (At == UINT64_MAX) throw std::overflow_error("process host boundary exhausted");
        ++At;
        Effects.clear();
        Counters = {};
    }
    Running &Owner(uint64_t id) {
        for (auto &root : Roots) if (root->Instance->State().Id == id) return *root;
        throw std::logic_error("process carrier has no owning instance");
    }
    void Queue(Running &root, process::Outcome outcome, process::RequestId request = {},
               process::Position hit = {}, process::Subscription subscription = {}, process::EffectHandle handle = {}) {
        process::HostEvent event{0, At, root.Instance->State().Id, outcome, request, subscription, hit, handle};
        if (!Observable(root, event)) return;
        // At most one admission, one terminal carrier event and one wake.
        const bool admission = outcome == process::Outcome::Applied || outcome == process::Outcome::Rejected;
        for (const auto &old : root.Events) {
            const bool oldAdmission = old.Result == process::Outcome::Applied || old.Result == process::Outcome::Rejected;
            if (old.Request == request && old.WakeSubscription == subscription && oldAdmission == admission) return;
        }
        if (root.Events.size() == 3 || root.NextEvent == UINT64_MAX)
            throw std::logic_error("process notification bound violated");
        event.Sequence = root.NextEvent++;
        root.Events.push_back(event);
    }
    void RemoveOwned(Running &root, World &world) {
        const auto id = root.Instance->State().Id;
        world.StopFieldSources(id);
        std::erase_if(Bolts, [&](const auto &bolt) { return bolt.Request.Instance == id; });
        root.Events.clear();
    }
    void Cancel(Running &root) {
        root.Instance->Cancel();
        root.Prepared = false;
    }
    void Retire() {
        std::erase_if(Roots, [](const auto &root) { return root->Instance->State().Status == process::Phase::Ended; });
    }
    bool CanActivate(const Running &root, const World &world) const {
        const auto bounds = world.GetSimulationBounds();
        const auto resident = [&](WorldPosition position, double radius) {
            const auto cell = position.Cell();
            return ContainsDisk(bounds, position, radius) && world.TryGetCell(cell.X, cell.Y).has_value();
        };
        if (!resident(root.Carrier.Origin, root.Carrier.ActivityRadius)
            || !resident(WorldPoint(root.Instance->State().Origin), 0)) return false;
        const auto id = root.Instance->State().Id;
        for (const auto &source : world.GetEnvironment().GetFieldSources().Sources()) {
            if (source.Owner != id) continue;
            const auto cell = source.Spec.Center.Cell();
            if (!FieldSources::Fits(source.Spec, bounds) || !world.TryGetCell(cell.X, cell.Y)) return false;
        }
        for (const auto &bolt : Bolts)
            if (bolt.Request.Instance == id && !resident(bolt.Position, FormulaWorldAdapter::BoltRadius)) return false;
        return true;
    }
    ProcessRefusal Validate(const Running &root, const process::EffectRequest &request, const World &world) const {
        if (!(root.Carrier.Grants & process::Mask(CapabilityOf(request.Description)))) return ProcessRefusal::Capability;
        // Stopping a resource never needs another paid credit or new geometry.
        if (std::holds_alternative<process::StopField>(request.Description)) return ProcessRefusal::None;
        double radius = FormulaWorldAdapter::BoltRadius;
        if (const auto *dig = std::get_if<process::Dig>(&request.Description)) radius = dig->Radius;
        if (const auto *wind = std::get_if<process::Wind>(&request.Description)) {
            radius = wind->Radius;
            if (std::hypot(wind->VelocityX, wind->VelocityY) > 600) return ProcessRefusal::Geometry;
        }
        if (const auto *bolt = std::get_if<process::Bolt>(&request.Description)) {
            if (std::abs(bolt->VelocityX) > 600 || bolt->Lifetime > 3600 || bolt->Damage > 1e6)
                return ProcessRefusal::Geometry;
        }
        const auto *source = std::get_if<process::FieldSource>(&request.Description);
        if (const auto *update = std::get_if<process::UpdateField>(&request.Description)) source = &update->Source;
        if (source) {
            radius = source->Radius;
            // Validate the actual float representation accepted by Environment.
            if (radius <= 0 || source->Lifetime > 3600 ||
                std::hypot(double(float(source->AccelerationX)), double(float(source->AccelerationY))) > 36000)
                return ProcessRefusal::Geometry;
        }
        if (radius > root.Carrier.MaximumRadius || radius > 16) return ProcessRefusal::Geometry;
        const auto origin = WorldPoint(request.Origin);
        if (!ContainsDisk(root.Carrier.Scope, origin, radius)
            || !ContainsDisk(world.GetSimulationBounds(), origin, radius)) return ProcessRefusal::Scope;
        if (!world.TryGetCell(request.Origin.CellX, request.Origin.CellY)) return ProcessRefusal::NotReady;
        if (root.Carrier.Credits == 0) return ProcessRefusal::Resources;
        return ProcessRefusal::None;
    }
    void Evaluate(Running &root, const World &world) {
        if (root.Suspended) return;
        const process::HostView view{At, (root.Carrier.Grants & process::Mask(process::Capability::Inventory))
            ? std::optional(root.Carrier.Credits) : std::nullopt};
        root.Slice = root.Instance->Step(view, root.Events);
        root.Events.clear();
        root.Prepared = true;
        Counters.Work += root.Slice.WorkUsed;
        Counters.Queries += root.Slice.QueriesUsed;
        if (root.Slice.Fault) {
            if (Diagnostics.size() == MaximumReceipts) Diagnostics.pop_front();
            const auto &fault = *root.Slice.Fault;
            Diagnostics.push_back({0, root.Instance->State().Id, root.Instance->State().ActiveSteps,
                fault.Resource + ":" + std::to_string(fault.Source.Line) + ":" + std::to_string(fault.Source.Column) + " " + fault.Message});
        }
        for (size_t i = 0; i < root.Slice.Count; ++i) {
            root.BirthContacts[i] = {};
            const auto &request = root.Slice.Requests[i];
            if (std::holds_alternative<process::Bolt>(request.Description)
                && Validate(root, request, world) == ProcessRefusal::None) {
                const auto position = WorldPoint(request.Origin);
                root.BirthContacts[i] = FormulaWorldAdapter::Sweep(world, position, position);
            }
        }
    }
    void Sweep(const World &world) {
        Collisions.clear();
        for (auto it = Bolts.begin(); it != Bolts.end();) {
            auto &root = Owner(it->Request.Instance);
            if (root.Suspended) { ++it; continue; }
            if (--it->Remaining == 0) {
                Queue(root, process::Outcome::Expired, it->Request);
                it = Bolts.erase(it);
                continue;
            }
            const auto to = FormulaWorldAdapter::Offset(it->Position, {it->Velocity.X / 60, it->Velocity.Y / 60});
            auto contact = FormulaWorldAdapter::Sweep(world, it->Position, to);
            // Scope exit competes with terrain by time of contact. A wall hit
            // inside the scope still wins over a later unauthorized segment.
            if (!ContainsDisk(root.Carrier.Scope, to, FormulaWorldAdapter::BoltRadius)) {
                const auto exit = ScopeExit(root.Carrier.Scope, it->Position, to);
                if (contact.Kind == FormulaContactKind::None || exit <= contact.Fraction) {
                    const auto position = FormulaWorldAdapter::Offset(it->Position,
                        {it->Velocity.X / 60 * exit, it->Velocity.Y / 60 * exit});
                    contact = {FormulaContactKind::OutOfSimulation, exit, position, position.Cell()};
                }
            }
            if (contact.Kind == FormulaContactKind::None) {
                it->Position = to;
                ++it;
            } else {
                Collisions.push_back({it->Request, contact});
                it = Bolts.erase(it);
            }
        }
        std::sort(Collisions.begin(), Collisions.end(), [](const auto &a, const auto &b) {
            return std::tuple(a.Contact.Fraction, a.Request.Instance, a.Request.Sequence, a.Contact.Cell.Y, a.Contact.Cell.X)
                < std::tuple(b.Contact.Fraction, b.Request.Instance, b.Request.Sequence, b.Contact.Cell.Y, b.Contact.Cell.X);
        });
        for (const auto &collision : Collisions)
            Queue(Owner(collision.Request.Instance), collision.Contact.Kind == FormulaContactKind::Solid
                ? process::Outcome::Hit : process::Outcome::Cancelled,
                collision.Request, LanguagePosition(collision.Contact.Position));
    }
    void RecordEffect(const Running &root, const process::EffectRequest &request, api::FactPayload payload) {
        if (Effects.size() == MaximumEffects) throw std::logic_error("process effect record bound violated");
        Effects.push_back({request.Id.Instance, root.Version, request.Id.Sequence, root.Activation, std::move(payload), root.Resource});
    }
    void Apply(Running &root, size_t index, World &world, const std::optional<MiningCollector> &playerCollector) {
        const auto &request = root.Slice.Requests[index];
        auto refusal = Validate(root, request, world);
        const auto origin = WorldPoint(request.Origin);
        uint32_t changed = 0;
        process::EffectHandle handle;
        if (refusal == ProcessRefusal::None) {
            if (const auto *dig = std::get_if<process::Dig>(&request.Description)) {
                const auto result = FormulaWorldAdapter::Dig(world, origin, dig->Radius,
                    root.Carrier.Kind == ProcessCarrierKind::Player ? playerCollector : std::nullopt);
                changed = result.Applied;
                if (changed) RecordEffect(root, request, api::MaterialRemovedInDisk{PublicPosition(request.Origin), dig->Radius,
                    result.Applied, result.Blocked, result.NotReady});
                else refusal = result.InventoryFull ? ProcessRefusal::Capacity :
                    result.NotReady ? ProcessRefusal::NotReady : ProcessRefusal::Blocked;
            } else if (const auto *wind = std::get_if<process::Wind>(&request.Description)) {
                if (WindCallsThisFrame == MaximumWindPulsesPerFrame) refusal = ProcessRefusal::Capacity;
                auto result = FormulaWindResult::Unavailable;
                if (refusal == ProcessRefusal::None) {
                    ++WindCallsThisFrame;
                    result = FormulaWorldAdapter::Wind(world, origin, wind->Radius, {wind->VelocityX, wind->VelocityY});
                }
                if (result == FormulaWindResult::Applied)
                    RecordEffect(root, request, api::WindPulseSubmitted{PublicPosition(request.Origin), wind->Radius,
                        {wind->VelocityX, wind->VelocityY}, {0, request.Id.Instance}});
                else if (refusal == ProcessRefusal::None) refusal = ProcessRefusal::WindUnavailable;
            } else if (const auto *bolt = std::get_if<process::Bolt>(&request.Description)) {
                if (Bolts.size() == MaximumProjectiles) refusal = ProcessRefusal::Capacity;
                else {
                    const auto &contact = root.BirthContacts[index];
                    if (contact.Kind == FormulaContactKind::None)
                        Bolts.push_back({request.Id, origin, Velocity(root.Carrier, bolt->VelocityX), bolt->Damage, bolt->Lifetime});
                }
            } else {
                const auto owner = root.Instance->State().Id;
                const auto spec = [&](const process::FieldSource &value) {
                    return FieldSourceSpec{origin, value.Radius,
                        {float(value.AccelerationX), float(value.AccelerationY)}, value.Lifetime};
                };
                if (const auto *source = std::get_if<process::FieldSource>(&request.Description)) {
                    const auto installed = world.InstallFieldSource(owner, spec(*source));
                    if (!installed) refusal = ProcessRefusal::Capacity;
                    else {
                        handle.Id = installed->Id;
                        RecordEffect(root, request, api::FieldSourceChanged{handle.Id, api::FieldSourceEvent::Installed});
                    }
                } else if (const auto *update = std::get_if<process::UpdateField>(&request.Description)) {
                    if (!world.UpdateFieldSource(owner, {update->Handle.Id}, spec(update->Source))) refusal = ProcessRefusal::InvalidHandle;
                    else {
                        handle = update->Handle;
                        RecordEffect(root, request, api::FieldSourceChanged{handle.Id, api::FieldSourceEvent::Updated});
                    }
                } else {
                    const auto &stop = std::get<process::StopField>(request.Description);
                    if (!world.StopFieldSource(owner, {stop.Handle.Id})) refusal = ProcessRefusal::InvalidHandle;
                    else RecordEffect(root, request, api::FieldSourceChanged{stop.Handle.Id, api::FieldSourceEvent::Stopped});
                }
            }
        }
        const auto outcome = refusal == ProcessRefusal::None ? process::Outcome::Applied : process::Outcome::Rejected;
        ++Counters.Requests;
        if (refusal == ProcessRefusal::None) {
            if (!std::holds_alternative<process::StopField>(request.Description)) --root.Carrier.Credits;
            ++Counters.Accepted;
        }
        else ++Counters.Rejected;
        if (Receipts.size() == MaximumReceipts) Receipts.pop_front();
        Receipts.push_back({At, request.Id, root.Carrier.Kind, root.Carrier.Subject, outcome, refusal, changed});
        Queue(root, outcome, request.Id, {}, {}, handle);
        if (refusal == ProcessRefusal::None && std::holds_alternative<process::Bolt>(request.Description)) {
            const auto &contact = root.BirthContacts[index];
            if (contact.Kind != FormulaContactKind::None)
                Queue(root, contact.Kind == FormulaContactKind::Solid ? process::Outcome::Hit : process::Outcome::Cancelled,
                    request.Id, LanguagePosition(contact.Position));
        }
    }
};

ProcessHost::ProcessHost() : Data(std::make_unique<State>()) {}
ProcessHost::~ProcessHost() = default;
void ProcessHost::BeginFrame() { Data->WindCallsThisFrame = 0; }

bool ProcessHost::SynchronizeActivity(World &world) {
    if (world.GetEnvironment().IsWaitingForBudget())
        throw std::logic_error("process activity change during pending environment step");
    bool changed = false;
    for (auto &root : Data->Roots) {
        if (root->Prepared) throw std::logic_error("process activity change before effects commit");
        const bool suspended = !Data->CanActivate(*root, world);
        if (root->Suspended == suspended) continue;
        root->Suspended = suspended;
        world.SuspendFieldSources(root->Instance->State().Id, suspended);
        changed = true;
    }
    return changed;
}

bool ProcessHost::Start(const process::ProgramArtifact &program, uint64_t id, uint64_t version,
                        const ProcessCarrier &carrier, const World &world,
                        std::optional<api::RequestKey> request, std::string &error, uint64_t resource) {
    // These are trusted host values, but Cell/RelativeTo assume normalized,
    // supported coordinates. Fail before those integer conversions/subtractions.
    auto position = carrier.Origin;
    position.Normalize();
    if (!resource || !version || !carrier.Subject || carrier.Credits < 0 || !std::isfinite(carrier.AngleDegrees)
        || !std::isfinite(carrier.MaximumRadius) || carrier.MaximumRadius < 0 || carrier.MaximumRadius > 16
        || !std::isfinite(carrier.ActivityRadius) || carrier.ActivityRadius < 0 || carrier.ActivityRadius > 16
        || carrier.Scope.Empty() || carrier.Scope.Width > 2048 || carrier.Scope.Height > 2048
        || carrier.Scope.X > INT64_MAX - carrier.Scope.Width || carrier.Scope.Y > INT64_MAX - carrier.Scope.Height
        || carrier.Limits.Outputs > 16 || carrier.Limits.Work > 256 || carrier.Limits.Queries > 8
        || carrier.Kind < ProcessCarrierKind::Player || carrier.Kind > ProcessCarrierKind::Device)
        throw std::invalid_argument("invalid trusted process carrier configuration");
    auto lower = WorldPoint({carrier.Scope.X, carrier.Scope.Y});
    auto upper = WorldPoint({carrier.Scope.X + carrier.Scope.Width, carrier.Scope.Y + carrier.Scope.Height});
    lower.Normalize();
    upper.Normalize();
    if (Data->Roots.size() == MaximumInstances) { error = "ProcessInstanceLimit"; return false; }
    const auto origin = position.Cell();
    if (!carrier.Scope.Contains(origin) || !ContainsDisk(world.GetSimulationBounds(), position, carrier.ActivityRadius)) {
        error = "ProcessScope"; return false;
    }
    if (!world.TryGetCell(origin.X, origin.Y)) { error = "ProcessNotReady"; return false; }
    auto result = process::Instance::Start(program, id, carrier.Grants, carrier.Limits, LanguagePosition(position));
    if (const auto *diagnostic = std::get_if<process::Diagnostic>(&result)) { error = diagnostic->Message; return false; }
    auto root = std::make_unique<Running>();
    root->Instance = std::move(std::get<std::unique_ptr<process::Instance>>(result));
    root->Carrier = carrier;
    root->Carrier.Origin = position;
    root->Version = version;
    root->Resource = resource;
    root->Activation = request;
    root->Events.reserve(3);
    Data->Begin();
    Data->Roots.push_back(std::move(root));
    Data->Evaluate(*Data->Roots.back(), world);
    error.clear();
    return true;
}

void ProcessHost::Prepare(const World &world) {
    Data->Begin();
    for (auto &root : Data->Roots) Data->Evaluate(*root, world);
    Data->Sweep(world);
}
void ProcessHost::Commit(World &world, const std::optional<MiningCollector> &playerCollector) {
    for (auto &root : Data->Roots) {
        if (root->Prepared)
            for (size_t i = 0; i < root->Slice.Count; ++i) Data->Apply(*root, i, world, playerCollector);
        root->Prepared = false;
        if (root->Instance->State().Status == process::Phase::Ended) Data->RemoveOwned(*root, world);
    }
    Data->Retire();
}
void ProcessHost::Clear(World &world) {
    for (auto &root : Data->Roots) {
        Data->Cancel(*root);
        Data->RemoveOwned(*root, world);
    }
    Data->Roots.clear();
    Data->Effects.clear();
    Data->Receipts.clear();
    Data->Diagnostics.clear();
    Data->Counters = {};
}
bool ProcessHost::HasCarrier(ProcessCarrierKind kind, uint64_t subject) const {
    return std::any_of(Data->Roots.begin(), Data->Roots.end(), [&](const auto &root) {
        return root->Carrier.Kind == kind && root->Carrier.Subject == subject;
    });
}
void ProcessHost::CancelCarrier(World &world, ProcessCarrierKind kind, uint64_t subject) {
    Data->Effects.clear();
    for (auto &root : Data->Roots)
        if (root->Carrier.Kind == kind && root->Carrier.Subject == subject) {
            Data->Cancel(*root);
            Data->RemoveOwned(*root, world);
        }
    Data->Retire();
}
void ProcessHost::WakeCarrier(ProcessCarrierKind kind, uint64_t subject) {
    for (auto &root : Data->Roots)
        if (!root->Suspended && root->Carrier.Kind == kind && root->Carrier.Subject == subject)
            if (const auto subscription = root->Instance->State().WakeSubscription)
                Data->Queue(*root, process::Outcome::Wake, {}, {}, *subscription);
}
void ProcessHost::AppendProjectiles(std::vector<FormulaProjectile> &output, uint64_t epoch) const {
    for (const auto &bolt : Data->Bolts) {
        const auto &root = Data->Owner(bolt.Request.Instance);
        if (root.Suspended) continue;
        output.push_back({epoch, bolt.Request.Instance, bolt.Request.Sequence, root.Version,
            bolt.Position, FormulaWorldAdapter::BoltRadius, bolt.Damage, bolt.Velocity, root.Resource});
    }
}
std::span<const FormulaEffectRecord> ProcessHost::Effects() const { return Data->Effects; }
const std::deque<ProcessReceipt> &ProcessHost::Receipts() const { return Data->Receipts; }
const std::deque<FormulaRuntimeDiagnostic> &ProcessHost::Diagnostics() const { return Data->Diagnostics; }
ProcessHostStats ProcessHost::Stats() const {
    auto result = Data->Counters;
    result.Instances = Data->Roots.size();
    result.Projectiles = Data->Bolts.size();
    result.RetainedBytes = sizeof(State) + Data->Roots.capacity() * sizeof(std::unique_ptr<Running>)
        + Data->Bolts.capacity() * sizeof(Projectile) + Data->Collisions.capacity() * sizeof(Collision)
        + Data->Effects.capacity() * sizeof(FormulaEffectRecord) + Data->Receipts.size() * sizeof(ProcessReceipt);
    for (const auto &root : Data->Roots) {
        result.SuspendedInstances += root->Suspended;
        result.PendingEvents += root->Events.size();
        result.RetainedBytes += sizeof(Running) + root->Events.capacity() * sizeof(process::HostEvent);
    }
    for (const auto &diagnostic : Data->Diagnostics)
        result.RetainedBytes += sizeof(FormulaRuntimeDiagnostic) + diagnostic.Message.capacity() + 1;
    return result;
}

std::vector<ProcessInspection> ProcessHost::Inspect() const {
    std::vector<ProcessInspection> result;
    result.reserve(Data->Roots.size());
    for (const auto &root : Data->Roots) {
        ProcessInspection item{root->Instance->Program().Identity(), root->Instance->State(), root->Version,
            root->Carrier.Kind, root->Carrier.Subject, root->Carrier.Credits, root->Suspended,
            root->Resource, root->Carrier, {}};
        const auto add = [&](process::EffectHandle handle) {
            if (handle.Id && std::find(item.ReferencedFieldHandles.begin(), item.ReferencedFieldHandles.end(), handle.Id)
                == item.ReferencedFieldHandles.end()) item.ReferencedFieldHandles.push_back(handle.Id);
        };
        const auto snapshot = root->Instance->ExportState();
        item.WaitingFor = snapshot.WaitingFor;
        add(snapshot.State.LastEffectHandle);
        for (const auto &local : snapshot.Locals) {
            if (!local) continue;
            if (const auto *handle = std::get_if<process::EffectHandle>(&*local)) add(*handle);
            if (const auto *effect = std::get_if<process::Effect>(&*local)) {
                if (const auto *update = std::get_if<process::UpdateField>(effect)) add(update->Handle);
                if (const auto *stop = std::get_if<process::StopField>(effect)) add(stop->Handle);
            }
        }
        for (const auto *event : {&snapshot.Receipt, &snapshot.Impact, &snapshot.Wake}) if (*event) add((*event)->Handle);
        for (const auto &event : root->Events) add(event.Handle);
        result.push_back(std::move(item));
    }
    return result;
}

std::string ProcessHost::CaptureState() const {
    checkpoint::Writer out;
    out.String("Pixel.ProcessHost"); out.Numbers(1, Data->At, Data->Roots.size());
    for (const auto &root : Data->Roots) {
        if (root->Prepared) throw std::logic_error("cannot checkpoint uncommitted process effects");
        checkpoint::Write(out, root->Instance->Program());
        checkpoint::Write(out, root->Instance->ExportState());
        checkpoint::Write(out, root->Carrier);
        out.Numbers(root->Version, root->Resource, root->NextEvent, root->Suspended, bool(root->Activation));
        if (root->Activation) out.Numbers(root->Activation->WorldEpoch, root->Activation->Caller, root->Activation->Sequence);
        out.Numbers(root->Events.size());
        for (const auto &event : root->Events) checkpoint::Write(out, event);
    }
    out.Numbers(Data->Bolts.size());
    for (const auto &bolt : Data->Bolts) {
        out.Numbers(bolt.Request.Instance, bolt.Request.Sequence); checkpoint::Write(out, bolt.Position);
        out.Numbers(bolt.Velocity.X, bolt.Velocity.Y, bolt.Damage, bolt.Remaining);
    }
    out.Numbers(Data->Receipts.size());
    for (const auto &receipt : Data->Receipts) {
        out.Numbers(receipt.At, receipt.Request.Instance, receipt.Request.Sequence); out.Enum(receipt.Carrier);
        out.Numbers(receipt.Subject); out.Enum(receipt.Outcome); out.Enum(receipt.Refusal); out.Numbers(receipt.ChangedCells);
    }
    out.Numbers(Data->Diagnostics.size());
    for (const auto &diagnostic : Data->Diagnostics) {
        out.Numbers(diagnostic.WorldEpoch, diagnostic.CastId, diagnostic.Tick); out.String(diagnostic.Message);
    }
    return out.Finish();
}

void ProcessHost::RestoreState(std::string_view encoded) {
    if (!Data->Roots.empty()) throw std::logic_error("process restore requires an empty host");
    checkpoint::Reader in(encoded);
    using checkpoint::Require;
    Require(in.String(32) == "Pixel.ProcessHost" && in.Number<uint32_t>() == 1, "unsupported process host checkpoint");
    auto restored = std::make_unique<State>();
    restored->At = in.Number<uint64_t>();
    Require(restored->At != UINT64_MAX, "exhausted checkpoint process boundary");
    const auto count = in.Count(MaximumInstances);
    uint64_t previousId = 0;
    for (size_t i = 0; i < count; ++i) {
        auto program = checkpoint::ReadProgram(in);
        const auto snapshot = checkpoint::ReadInstance(in);
        auto root = std::make_unique<Running>();
        root->Carrier = checkpoint::ReadCarrier(in);
        in.Numbers(root->Version, root->Resource, root->NextEvent); root->Suspended = in.Boolean();
        Require(root->Version != 0 && root->Resource != 0 && root->NextEvent > snapshot.EventSequence
            && root->NextEvent != UINT64_MAX && snapshot.State.Id > previousId && snapshot.State.Id != UINT64_MAX
            && snapshot.LastBoundary > 0 && snapshot.LastBoundary <= restored->At
            && snapshot.State.ActiveSteps <= snapshot.LastBoundary && snapshot.Limits.Work == root->Carrier.Limits.Work
            && snapshot.Limits.Queries == root->Carrier.Limits.Queries && snapshot.Limits.Outputs == root->Carrier.Limits.Outputs,
            "inconsistent checkpoint process identity, clock or budget");
        previousId = snapshot.State.Id;
        if (in.Boolean()) {
            api::RequestKey request; in.Numbers(request.WorldEpoch, request.Caller, request.Sequence);
            Require(request.WorldEpoch && request.Caller && request.Sequence, "invalid checkpoint activation request");
            root->Activation = request;
        }
        auto instance = process::Instance::Restore(std::move(program), snapshot, root->Carrier.Grants);
        if (const auto *error = std::get_if<process::Diagnostic>(&instance)) throw std::invalid_argument(error->Message);
        root->Instance = std::move(std::get<std::unique_ptr<process::Instance>>(instance));
        const auto events = in.Count(3);
        root->Events.reserve(3);
        uint64_t previousEvent = snapshot.EventSequence;
        for (size_t j = 0; j < events; ++j) {
            const auto event = checkpoint::ReadEvent(in);
            const bool requestEvent = event.Request.Sequence != 0;
            Require(event.Target == snapshot.State.Id && event.Sequence > previousEvent && event.Sequence < root->NextEvent
                && event.CommittedAt >= snapshot.LastBoundary && event.CommittedAt <= restored->At && Observable(*root, event)
                && (requestEvent ? event.WakeSubscription == process::Subscription{} : event.Request == process::RequestId{})
                && (requestEvent ? event.Result >= process::Outcome::Applied && event.Result <= process::Outcome::Cancelled
                                 : event.Result == process::Outcome::Wake)
                && (!event.Handle.Id || (requestEvent && event.Result == process::Outcome::Applied)),
                "invalid checkpoint pending process event");
            const bool admission = event.Result == process::Outcome::Applied || event.Result == process::Outcome::Rejected;
            for (const auto &old : root->Events) {
                const bool oldAdmission = old.Result == process::Outcome::Applied || old.Result == process::Outcome::Rejected;
                Require(old.Request != event.Request || old.WakeSubscription != event.WakeSubscription || oldAdmission != admission,
                    "duplicate checkpoint process notification");
            }
            previousEvent = event.Sequence;
            root->Events.push_back(event);
        }
        restored->Roots.push_back(std::move(root));
    }
    const auto bolts = in.Count(MaximumProjectiles);
    for (size_t i = 0; i < bolts; ++i) {
        Projectile bolt;
        in.Numbers(bolt.Request.Instance, bolt.Request.Sequence); bolt.Position = checkpoint::ReadWorldPosition(in);
        in.Numbers(bolt.Velocity.X, bolt.Velocity.Y, bolt.Damage, bolt.Remaining);
        const auto owner = std::find_if(restored->Roots.begin(), restored->Roots.end(), [&](const auto &root) {
            return root->Instance->State().Id == bolt.Request.Instance;
        });
        Require(owner != restored->Roots.end() && bolt.Request.Sequence > 0
            && bolt.Request.Sequence <= (*owner)->Instance->State().LastRequest.Sequence
            && bolt.Remaining > 0 && bolt.Remaining <= 3600 && bolt.Damage >= 0 && bolt.Damage <= 1e6
            && std::hypot(bolt.Velocity.X, bolt.Velocity.Y) <= 600.000000000001
            && ContainsDisk((*owner)->Carrier.Scope, bolt.Position, FormulaWorldAdapter::BoltRadius),
            "invalid checkpoint projectile ownership, geometry or lifetime");
        for (const auto &old : restored->Bolts) Require(old.Request != bolt.Request, "duplicate checkpoint projectile identity");
        restored->Bolts.push_back(bolt);
    }
    const auto receipts = in.Count(MaximumReceipts);
    for (size_t i = 0; i < receipts; ++i) {
        ProcessReceipt receipt;
        in.Numbers(receipt.At, receipt.Request.Instance, receipt.Request.Sequence); receipt.Carrier = in.Enum(ProcessCarrierKind::Device);
        receipt.Subject = in.Number<uint64_t>(); receipt.Outcome = in.Enum(process::Outcome::Timeout);
        receipt.Refusal = in.Enum(ProcessRefusal::InvalidHandle); receipt.ChangedCells = in.Number<uint32_t>();
        Require(receipt.At <= restored->At && receipt.Request.Instance && receipt.Request.Sequence && receipt.Subject
            && (receipt.Outcome == process::Outcome::Applied || receipt.Outcome == process::Outcome::Rejected)
            && ((receipt.Outcome == process::Outcome::Applied) == (receipt.Refusal == ProcessRefusal::None)),
            "invalid checkpoint receipt history");
        restored->Receipts.push_back(receipt);
    }
    const auto diagnostics = in.Count(MaximumReceipts);
    for (size_t i = 0; i < diagnostics; ++i) {
        FormulaRuntimeDiagnostic diagnostic;
        in.Numbers(diagnostic.WorldEpoch, diagnostic.CastId, diagnostic.Tick); diagnostic.Message = in.String(16384);
        restored->Diagnostics.push_back(std::move(diagnostic));
    }
    in.End();
    Data = std::move(restored);
}
} // namespace pixel
