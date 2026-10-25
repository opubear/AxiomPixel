#include "FormulaBridge.h"
#include "ProcessHost.h"
#include "CheckpointCodec.h"
#include "Runtime/Gameplay/FormulaValidation/FormulaAnalysis.h"
#include "Runtime/Simulation/World/WorldStorageError.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <tuple>

namespace pixel {
namespace {
constexpr size_t MaxRoots = 16, MaxEffects = 256, MaxWind = 64, MaxActivations = 8, MaxDiagnostics = 256;
struct Root {
    uint64_t Id, Version;
    uint64_t NextEmission = 1;
    std::optional<api::RequestKey> ActivationRequest;
    WorldPosition Anchor;
    formula::validation::Analysis Budget;
    formula::Simulator Simulator;
    std::vector<formula::TraceEntry> Batch;
    bool Suspended = false;
};
struct Collision {
    Root *Owner;
    formula::ProjectileId Projectile;
    FormulaContact Contact;
};
api::Position PublicPosition(const WorldPosition &position) {
    const auto cell = position.Cell();
    return {{cell.X, cell.Y}, position.RelativeTo(cell)};
}
bool ActivePosition(const World &world, WorldPosition position, double radius = 0) {
    const auto cell = position.Cell();
    return world.GetSimulationBounds().Contains(cell) && world.TryGetCell(cell.X, cell.Y)
        && FieldSources::Fits({position, radius, {}, 0}, world.GetSimulationBounds());
}
} // namespace

struct FormulaBridge::State {
    ProcessHost Processes;
    std::optional<MiningCollector> PlayerCollector;
    std::optional<formula::process::ProgramArtifact> ActiveProcess;
    formula::Formula Active = formula::Formula::Bolt();
    formula::validation::Analysis ActiveBudget;
    uint64_t Version = 1, Epoch = 1, NextCast = 1;
    uint64_t RetiredWind = 0, Accepted = 0, Rejected = 0, WindCalls = 0;
    std::string Error;
    std::vector<std::unique_ptr<Root>> Roots;
    std::vector<FormulaProjectile> Projectiles;
    std::deque<FormulaRuntimeDiagnostic> Diagnostics;
    std::array<FormulaEffectRecord, FormulaBridge::MaximumEffects> Effects;
    size_t EffectCount = 0;

    State() {
        const auto analysis = formula::validation::Analyze(Active);
        if (!analysis)
            throw std::logic_error("default bolt failed gameplay analysis");
        ActiveBudget = analysis.Budget;
        Roots.reserve(MaxRoots);
        Projectiles.reserve(FormulaBridge::MaximumEffects);
    }

    void Record(uint64_t cast, formula::Tick tick, std::string message) {
        if (Diagnostics.size() == MaxDiagnostics)
            Diagnostics.pop_front();
        Diagnostics.push_back({Epoch, cast, tick, std::move(message)});
    }

    bool Reject(std::string reason) {
        Error = std::move(reason);
        ++Rejected;
        Record(0, 0, Error);
        return false;
    }

    FormulaRuntimeStats Statistics() const {
        FormulaRuntimeStats result;
        const auto process = Processes.Stats();
        result.Roots = Roots.size() + process.Instances;
        result.Pending = process.Instances;
        result.Projectiles = Projectiles.size();
        result.SnapshotBytes = Projectiles.capacity() * sizeof(FormulaProjectile);
        result.EffectRecordBytes = sizeof(Effects);
        for (const auto &entry : Diagnostics)
            result.DiagnosticBytes += sizeof(FormulaRuntimeDiagnostic) + entry.Message.capacity() + 1;
        result.RetiredWind = RetiredWind;
        result.AcceptedThisFrame = Accepted;
        result.RejectedThisFrame = Rejected;
        result.WindCallsThisFrame = WindCalls;
        for (const auto &root : Roots) {
            result.ReservedEffects += root->Budget.Effects;
            result.ReservedWind += root->Budget.WindEffects;
            result.Pending += root->Simulator.PendingCount();
        }
        return result;
    }

    void Drain(Root &root) {
        auto events = root.Simulator.DrainTrace();
        root.Batch.insert(root.Batch.end(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
    }

    void Advance(Root &root, bool step, const World &world, std::vector<Collision> &collisions) {
        if (root.Suspended) return;
        std::map<formula::ProjectileId, formula::Vec2> old;
        for (const auto &projectile : root.Simulator.Projectiles())
            old.emplace(projectile.Id, projectile.Position);
        root.Simulator.AdvanceTo(root.Simulator.Now() + (step ? 1 : 0));
        Drain(root);

        // Expiry wins at the boundary, so the excluded final segment has no Hit.
        for (const auto &projectile : root.Simulator.Projectiles()) {
            const auto previous = old.find(projectile.Id);
            const auto to = FormulaWorldAdapter::Offset(root.Anchor, projectile.Position);
            const auto from = previous == old.end() ? to : FormulaWorldAdapter::Offset(root.Anchor, previous->second);
            auto contact = FormulaWorldAdapter::Sweep(world, from, to);
            if (contact.Kind != FormulaContactKind::None)
                collisions.push_back({&root, projectile.Id, std::move(contact)});
        }
    }

    void Resolve(std::vector<Collision> &collisions) {
        std::sort(collisions.begin(), collisions.end(), [](const Collision &a, const Collision &b) {
            return std::tuple(a.Contact.Fraction, a.Owner->Id, a.Projectile, a.Contact.Cell.Y, a.Contact.Cell.X)
                 < std::tuple(b.Contact.Fraction, b.Owner->Id, b.Projectile, b.Contact.Cell.Y, b.Contact.Cell.X);
        });
        for (const auto &collision : collisions) {
            auto &root = *collision.Owner;
            if (collision.Contact.Kind == FormulaContactKind::Solid) {
                const auto origin = root.Anchor.Cell();
                const auto local = collision.Contact.Position.RelativeTo(origin);
                const auto anchor = root.Anchor.RelativeTo(origin);
                if (!root.Simulator.Hit(collision.Projectile, {local[0] - anchor[0], local[1] - anchor[1]}))
                    throw std::logic_error("collision references a retired formula projectile");
            } else {
                if (!root.Simulator.DiscardProjectile(collision.Projectile))
                    throw std::logic_error("collision references a retired formula projectile");
                Record(root.Id, root.Simulator.Now(), collision.Contact.Kind == FormulaContactKind::NotReady ? "NotReady" : "OutOfSimulation");
            }
            Drain(root);
        }
    }

    uint64_t NextEffectEmission(Root &root) {
        if (root.NextEmission == UINT64_MAX)
            throw std::overflow_error("formula effect emission identity exhausted");
        return root.NextEmission++;
    }

    void RecordEffect(const Root &root, uint64_t emission, api::FactPayload payload) {
        if (EffectCount == Effects.size())
            throw std::logic_error("formula effects exceeded their analyzed reservation");
        Effects[EffectCount++] = {root.Id, root.Version, emission, root.ActivationRequest, std::move(payload)};
    }

    void Commit(Root &root, World &world) {
        // Analysis has validated every event. Internal errors propagate, including
        // after earlier effects committed; the host must not resume this session.
        for (const auto &event : root.Batch) {
            Record(root.Id, event.At, formula::FormatTrace(event));
            const auto position = FormulaWorldAdapter::Offset(root.Anchor, event.Position);
            double radius = 0;
            if (const auto *dig = std::get_if<formula::DigArea>(&event.Data)) radius = dig->Radius;
            if (const auto *wind = std::get_if<formula::WindImpulse>(&event.Data)) radius = wind->Radius;
            if (!ActivePosition(world, position, radius)) {
                Record(root.Id, event.At, "OutOfSimulation: effect crosses inactive area");
                continue;
            }
            if (const auto *dig = std::get_if<formula::DigArea>(&event.Data)) {
                const auto emission = NextEffectEmission(root);
                const auto result = FormulaWorldAdapter::Dig(world, position, dig->Radius, PlayerCollector);
                if (result.Applied != 0)
                    RecordEffect(root, emission, api::MaterialRemovedInDisk{PublicPosition(position), dig->Radius,
                        result.Applied, result.Blocked, result.NotReady});
                Record(root.Id, event.At, "Dig applied=" + std::to_string(result.Applied) + " blocked=" + std::to_string(result.Blocked)
                    + " notReady=" + std::to_string(result.NotReady) + " inventoryFull=" + std::to_string(result.InventoryFull));
            } else if (const auto *wind = std::get_if<formula::WindImpulse>(&event.Data)) {
                const auto emission = NextEffectEmission(root);
                const auto result = FormulaWorldAdapter::Wind(world, position, wind->Radius, wind->Velocity);
                if (result == FormulaWindResult::Applied) {
                    ++WindCalls;
                    RecordEffect(root, emission, api::WindPulseSubmitted{PublicPosition(position), wind->Radius,
                        {wind->Velocity.X, wind->Velocity.Y}, {0, root.Id}});
                }
                Record(root.Id, event.At, FormulaWorldAdapter::WindResultName(result));
            }
        }
        root.Batch.clear();
    }

    void CollectProcessEffects() {
        for (const auto &effect : Processes.Effects()) {
            if (EffectCount == Effects.size()) throw std::logic_error("bridge effect capacity exceeded");
            Effects[EffectCount++] = effect;
        }
    }

    void Refresh() {
        for (auto it = Roots.begin(); it != Roots.end();) {
            if ((*it)->Simulator.IsIdle()) {
                RetiredWind += (*it)->Budget.WindEffects;
                it = Roots.erase(it);
            } else {
                ++it;
            }
        }
        Projectiles.clear();
        for (const auto &root : Roots) {
            if (root->Suspended) continue;
            for (const auto &projectile : root->Simulator.Projectiles())
                Projectiles.push_back({Epoch, root->Id, projectile.Id, root->Version,
                    FormulaWorldAdapter::Offset(root->Anchor, projectile.Position), FormulaWorldAdapter::BoltRadius,
                    projectile.Damage, projectile.Velocity});
        }
        Processes.AppendProjectiles(Projectiles, Epoch);
    }
};

FormulaBridge::FormulaBridge() : Data(std::make_unique<State>()) {}
FormulaBridge::~FormulaBridge() = default;

void FormulaBridge::BindPlayerInventory(items::Inventory &inventory, items::ContainerId container) {
    if (Data->Statistics().Roots != 0) throw std::logic_error("cannot rebind running formula inventory");
    inventory.Reference(container);
    Data->PlayerCollector.emplace(MiningCollector{inventory, container});
}

bool FormulaBridge::Apply(const formula::Formula &description) {
    if (Data->Version == UINT64_MAX)
        throw std::overflow_error("formula program version exhausted");
    const auto result = formula::validation::Analyze(description);
    if (!result) {
        Data->Error = result.Errors.empty() ? "formula analysis failed" : result.Errors.front().Field + ": " + result.Errors.front().Message;
        return false;
    }
    Data->ActiveProcess.reset();
    Data->Active = description;
    Data->ActiveBudget = result.Budget;
    ++Data->Version;
    Data->Error.clear();
    return true;
}

void FormulaBridge::ApplyProcess(const formula::process::ProgramArtifact &program) {
    if (Data->Version == UINT64_MAX) throw std::overflow_error("formula program version exhausted");
    Data->ActiveProcess = program;
    ++Data->Version;
    Data->Error.clear();
}
api::ProgramRef FormulaBridge::AppliedProgram() const { return {Data->ActiveProcess ? 2u : 1u, Data->Version}; }

bool FormulaBridge::CastCarrier(World &world, const ProcessCarrier &carrier, std::optional<api::RequestKey> request) {
    if (!Data->ActiveProcess) return Data->Reject("ProcessProgramRequired");
    return CastProcess(world, *Data->ActiveProcess, AppliedProgram(), carrier, request);
}
bool FormulaBridge::CastProcess(World &world, const formula::process::ProgramArtifact &program, api::ProgramRef identity,
                               const ProcessCarrier &carrier, std::optional<api::RequestKey> request) {
    Data->EffectCount = 0;
    if (world.GetEnvironment().IsWaitingForBudget()) return Data->Reject("WaitingForBudget");
    if (Data->Accepted >= MaxActivations || Data->Statistics().Roots >= MaxRoots) return Data->Reject("ProcessActivationLimit");
    if (Data->NextCast == UINT64_MAX) throw std::overflow_error("formula activation identity exhausted");
    if (!Data->Processes.Start(program, Data->NextCast, identity.Version, carrier, world, request, Data->Error, identity.ResourceId))
        return Data->Reject(Data->Error);
    ++Data->NextCast;
    ++Data->Accepted;
    Data->Processes.Commit(world, Data->PlayerCollector);
    Data->CollectProcessEffects();
    Data->Refresh();
    return true;
}
bool FormulaBridge::HasCarrier(ProcessCarrierKind kind, uint64_t subject) const { return Data->Processes.HasCarrier(kind, subject); }
void FormulaBridge::CancelCarrier(World &world, ProcessCarrierKind kind, uint64_t subject) {
    Data->EffectCount = 0;
    Data->Processes.CancelCarrier(world, kind, subject);
    Data->Refresh();
}
void FormulaBridge::WakeCarrier(ProcessCarrierKind kind, uint64_t subject) { Data->Processes.WakeCarrier(kind, subject); }
ProcessHostStats FormulaBridge::ProcessStats() const { return Data->Processes.Stats(); }
std::vector<ProcessInspection> FormulaBridge::InspectProcesses() const { return Data->Processes.Inspect(); }
const std::optional<formula::process::ProgramArtifact> &FormulaBridge::ActiveProcess() const { return Data->ActiveProcess; }
const std::deque<ProcessReceipt> &FormulaBridge::ProcessReceipts() const { return Data->Processes.Receipts(); }
const std::deque<FormulaRuntimeDiagnostic> &FormulaBridge::ProcessDiagnostics() const { return Data->Processes.Diagnostics(); }

const formula::Formula &FormulaBridge::ActiveFormula() const { return Data->Active; }
uint64_t FormulaBridge::Version() const { return Data->Version; }
uint64_t FormulaBridge::LastActivationId() const { return Data->NextCast - 1; }
const std::string &FormulaBridge::LastError() const { return Data->Error; }
uint64_t FormulaBridge::WorldEpoch() const { return Data->Epoch; }

void FormulaBridge::BeginFrame() {
    Data->Processes.BeginFrame();
    Data->RetiredWind = 0;
    Data->Accepted = Data->Rejected = Data->WindCalls = 0;
}

bool FormulaBridge::Cast(World &world, WorldPosition anchor, double angleDegrees,
                          std::optional<api::RequestKey> activationRequest) {
    if (Data->ActiveProcess) {
        ProcessCarrier carrier;
        carrier.Origin = anchor;
        carrier.Scope = world.GetSimulationBounds();
        carrier.AngleDegrees = angleDegrees;
        return CastCarrier(world, carrier, activationRequest);
    }
    Data->EffectCount = 0;
    if (world.GetEnvironment().IsWaitingForBudget())
        return Data->Reject("WaitingForBudget: activation requires a committed material step");
    if (!std::isfinite(angleDegrees))
        return Data->Reject("InvalidAim: angle must be finite");
    const auto stats = Data->Statistics();
    if (stats.AcceptedThisFrame >= MaxActivations)
        return Data->Reject("ActivationLimit: at most 8 activations per frame");
    if (stats.Roots >= MaxRoots)
        return Data->Reject("RootLimit: at most 16 running activations");
    if (stats.ReservedEffects + Data->ActiveBudget.Effects > MaxEffects)
        return Data->Reject("EffectLimit: session reservation exceeds 256");
    if (stats.ReservedWind + stats.RetiredWind + Data->ActiveBudget.WindEffects > MaxWind)
        return Data->Reject("WindLimit: frame reservation exceeds 64");

    if (Data->NextCast == UINT64_MAX)
        throw std::overflow_error("formula activation identity exhausted");
    anchor.Normalize();
    auto root = std::make_unique<Root>();
    root->Id = Data->NextCast++;
    root->Version = Data->Version;
    root->ActivationRequest = activationRequest;
    root->Anchor = anchor;
    root->Budget = Data->ActiveBudget;
    root->Simulator.Cast(Data->Active, {}, angleDegrees);
    auto &active = *root;
    Data->Roots.push_back(std::move(root));
    ++Data->Accepted;
    std::vector<Collision> collisions;
    Data->Advance(active, false, world, collisions);
    Data->Resolve(collisions);
    Data->Commit(active, world);
    Data->Refresh();
    Data->Error.clear();
    return true;
}

void FormulaBridge::Step(World &world) {
    Data->EffectCount = 0;
    if (world.GetEnvironment().IsWaitingForBudget())
        throw std::logic_error("formula step requires a committed environment step");
    SynchronizeActivity(world);
    std::vector<Collision> collisions;
    // All sweeps see the same committed map, before any root's Dig is applied.
    for (auto &root : Data->Roots)
        Data->Advance(*root, true, world, collisions);
    Data->Resolve(collisions);
    Data->Processes.Prepare(world);
    for (auto &root : Data->Roots)
        Data->Commit(*root, world);
    Data->Processes.Commit(world, Data->PlayerCollector);
    Data->CollectProcessEffects();
    Data->Refresh();
}

bool FormulaBridge::SynchronizeActivity(World &world) {
    bool changed = Data->Processes.SynchronizeActivity(world);
    for (auto &root : Data->Roots) {
        bool active = ActivePosition(world, root->Anchor);
        for (const auto &projectile : root->Simulator.Projectiles())
            active = active && ActivePosition(world, FormulaWorldAdapter::Offset(root->Anchor, projectile.Position),
                                              FormulaWorldAdapter::BoltRadius);
        if (root->Suspended == !active) continue;
        root->Suspended = !active;
        changed = true;
    }
    if (changed) Data->Refresh();
    return changed;
}

void FormulaBridge::Clear(World &world) {
    Data->EffectCount = 0;
    for (const auto &root : Data->Roots)
        Data->RetiredWind += root->Budget.WindEffects;
    Data->Processes.Clear(world);
    Data->Roots.clear();
    Data->Projectiles.clear();
    Data->Diagnostics.clear();
    Data->Error.clear();
}

void FormulaBridge::OnWorldReplaced(World &world) {
    Clear(world);
    if (Data->Epoch == UINT64_MAX)
        throw std::overflow_error("formula diagnostic epoch exhausted");
    ++Data->Epoch;
}

const std::vector<FormulaProjectile> &FormulaBridge::Projectiles() const { return Data->Projectiles; }
const std::deque<FormulaRuntimeDiagnostic> &FormulaBridge::Diagnostics() const { return Data->Diagnostics; }
std::span<const FormulaEffectRecord> FormulaBridge::LastEffects() const {
    return {Data->Effects.data(), Data->EffectCount};
}
FormulaRuntimeStats FormulaBridge::Stats() const { return Data->Statistics(); }

std::string FormulaBridge::CaptureState() const {
    if (!Data->Roots.empty())
        throw WorldStorageError("cannot save live legacy formula instances; clear or finish them before saving");
    checkpoint::Writer out;
    out.String("Pixel.FormulaBridge"); out.Numbers(1, Data->Version, Data->Epoch, Data->NextCast);
    checkpoint::Write(out, Data->Active);
    out.Numbers(bool(Data->ActiveProcess));
    if (Data->ActiveProcess) checkpoint::Write(out, *Data->ActiveProcess);
    out.String(Data->Processes.CaptureState());
    return out.Finish();
}

void FormulaBridge::RestoreState(std::string_view encoded) {
    if (!Data->Roots.empty() || Data->Processes.Stats().Instances != 0)
        throw std::logic_error("formula restore requires an empty bridge");
    checkpoint::Reader in(encoded);
    checkpoint::Require(in.String(32) == "Pixel.FormulaBridge" && in.Number<uint32_t>() == 1,
        "unsupported formula bridge checkpoint");
    auto restored = std::make_unique<State>();
    in.Numbers(restored->Version, restored->Epoch, restored->NextCast);
    checkpoint::Require(restored->Version && restored->Epoch && restored->NextCast,
        "invalid checkpoint bridge identity");
    size_t nodes = 0;
    restored->Active = checkpoint::ReadFormula(in, nodes);
    const auto analysis = formula::validation::Analyze(restored->Active);
    checkpoint::Require(bool(analysis), "invalid checkpoint legacy formula description");
    restored->ActiveBudget = analysis.Budget;
    if (in.Boolean()) restored->ActiveProcess = checkpoint::ReadProgram(in);
    restored->Processes.RestoreState(in.String(checkpoint::MaximumBytes));
    in.End();
    for (const auto &instance : restored->Processes.Inspect())
        checkpoint::Require(instance.State.Id < restored->NextCast, "checkpoint activation identity would be reused");
    for (const auto &receipt : restored->Processes.Receipts())
        checkpoint::Require(receipt.Request.Instance < restored->NextCast, "checkpoint receipt exceeds activation identity");
    for (const auto &diagnostic : restored->Processes.Diagnostics())
        checkpoint::Require(diagnostic.CastId < restored->NextCast, "checkpoint diagnostic exceeds activation identity");
    // Inventory ownership belongs to Session and has already been rebound.
    if (Data->PlayerCollector) restored->PlayerCollector.emplace(*Data->PlayerCollector);
    restored->Refresh();
    Data = std::move(restored);
}
} // namespace pixel
