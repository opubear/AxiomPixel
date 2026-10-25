#include "FormulaBridge.h"
#include "ProcessHost.h"
#include "CheckpointCodec.h"
#include "DefaultProcess.h"
#include <stdexcept>

namespace pixel {
namespace {
constexpr size_t MaxActivations = 8;
}
struct FormulaBridge::State {
    ProcessHost Processes;
    std::optional<MiningCollector> PlayerCollector;
    formula::process::ProgramArtifact ActiveProcess = DefaultPlayerProcess();
    uint64_t Version = 1, Epoch = 1, NextCast = 1;
    uint64_t Accepted = 0, Rejected = 0;
    std::string Error;
    std::vector<FormulaProjectile> Projectiles;
    std::array<FormulaEffectRecord, FormulaBridge::MaximumEffects> Effects;
    size_t EffectCount = 0;

    bool Reject(std::string reason) {
        Error = std::move(reason);
        ++Rejected;
        return false;
    }
    FormulaRuntimeStats Statistics() const {
        FormulaRuntimeStats result;
        const auto process = Processes.Stats();
        result.Roots = process.Instances;
        result.Pending = process.Instances;
        result.Projectiles = Projectiles.size();
        result.SnapshotBytes = Projectiles.capacity() * sizeof(FormulaProjectile);
        result.EffectRecordBytes = sizeof(Effects);
        for (const auto &entry : Processes.Diagnostics())
            result.DiagnosticBytes += sizeof(FormulaRuntimeDiagnostic) + entry.Message.capacity() + 1;
        result.AcceptedThisFrame = Accepted;
        result.RejectedThisFrame = Rejected;
        return result;
    }
    void CollectProcessEffects() {
        for (const auto &effect : Processes.Effects()) {
            if (EffectCount == Effects.size()) throw std::logic_error("bridge effect capacity exceeded");
            Effects[EffectCount++] = effect;
        }
    }
    void Refresh() {
        Projectiles.clear();
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

void FormulaBridge::ApplyProcess(const formula::process::ProgramArtifact &program) {
    if (Data->Version == UINT64_MAX) throw std::overflow_error("formula program version exhausted");
    Data->ActiveProcess = program;
    ++Data->Version;
    Data->Error.clear();
}
api::ProgramRef FormulaBridge::AppliedProgram() const { return {2, Data->Version}; }
bool FormulaBridge::CastCarrier(World &world, const ProcessCarrier &carrier, std::optional<api::RequestKey> request) {
    return CastProcess(world, Data->ActiveProcess, AppliedProgram(), carrier, request);
}
bool FormulaBridge::CastProcess(World &world, const formula::process::ProgramArtifact &program, api::ProgramRef identity,
                               const ProcessCarrier &carrier, std::optional<api::RequestKey> request) {
    Data->EffectCount = 0;
    if (world.GetEnvironment().IsWaitingForBudget()) return Data->Reject("WaitingForBudget");
    if (Data->Accepted >= MaxActivations || Data->Statistics().Roots >= ProcessHost::MaximumInstances) return Data->Reject("ProcessActivationLimit");
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
std::vector<ProcessInspection> FormulaBridge::InspectProcesses() const { return Data->Processes.Inspect(); }
ProcessHostStats FormulaBridge::ProcessStats() const { return Data->Processes.Stats(); }
const std::deque<ProcessReceipt> &FormulaBridge::ProcessReceipts() const { return Data->Processes.Receipts(); }
const std::deque<FormulaRuntimeDiagnostic> &FormulaBridge::ProcessDiagnostics() const { return Data->Processes.Diagnostics(); }

uint64_t FormulaBridge::Version() const { return Data->Version; }
uint64_t FormulaBridge::LastActivationId() const { return Data->NextCast - 1; }
const std::string &FormulaBridge::LastError() const { return Data->Error; }
uint64_t FormulaBridge::WorldEpoch() const { return Data->Epoch; }

const formula::process::ProgramArtifact &FormulaBridge::ActiveProcess() const { return Data->ActiveProcess; }
void FormulaBridge::BeginFrame() {
    Data->Processes.BeginFrame();
    Data->Accepted = Data->Rejected = 0;
}
bool FormulaBridge::Cast(World &world, WorldPosition anchor, double angleDegrees,
                       std::optional<api::RequestKey> activationRequest) {
    ProcessCarrier carrier;
    carrier.Origin = anchor;
    carrier.Scope = world.GetSimulationBounds();
    carrier.AngleDegrees = angleDegrees;
    return CastCarrier(world, carrier, activationRequest);
}
void FormulaBridge::Step(World &world) {
    Data->EffectCount = 0;
    if (world.GetEnvironment().IsWaitingForBudget())
        throw std::logic_error("formula step requires a committed environment step");
    SynchronizeActivity(world);
    Data->Processes.Prepare(world);
    Data->Processes.Commit(world, Data->PlayerCollector);
    Data->CollectProcessEffects();
    Data->Refresh();
}
bool FormulaBridge::SynchronizeActivity(World &world) {
    const bool changed = Data->Processes.SynchronizeActivity(world);
    if (changed) Data->Refresh();
    return changed;
}
void FormulaBridge::Clear(World &world) {
    Data->EffectCount = 0;
    Data->Processes.Clear(world);
    Data->Projectiles.clear();
    Data->Error.clear();
}
void FormulaBridge::OnWorldReplaced(World &world) {
    Clear(world);
    if (Data->Epoch == UINT64_MAX)
        throw std::overflow_error("formula diagnostic epoch exhausted");
    ++Data->Epoch;
}

const std::vector<FormulaProjectile> &FormulaBridge::Projectiles() const { return Data->Projectiles; }
const std::deque<FormulaRuntimeDiagnostic> &FormulaBridge::Diagnostics() const { return Data->Processes.Diagnostics(); }
std::span<const FormulaEffectRecord> FormulaBridge::LastEffects() const {
    return {Data->Effects.data(), Data->EffectCount};
}
FormulaRuntimeStats FormulaBridge::Stats() const { return Data->Statistics(); }

std::string FormulaBridge::CaptureState() const {
    checkpoint::Writer out;
    out.String("Pixel.FormulaBridge");
    out.Numbers(2, Data->Version, Data->Epoch, Data->NextCast);
    checkpoint::Write(out, Data->ActiveProcess);
    out.String(Data->Processes.CaptureState());
    return out.Finish();
}
void FormulaBridge::RestoreState(std::string_view encoded) {
    if (Data->Processes.Stats().Instances != 0)
        throw std::logic_error("formula restore requires an empty bridge");
    checkpoint::Reader in(encoded);
    checkpoint::Require(in.String(32) == "Pixel.FormulaBridge" && in.Number<uint32_t>() == 2,
                        "unsupported formula bridge checkpoint; only process/1 is supported");
    auto restored = std::make_unique<State>();
    in.Numbers(restored->Version, restored->Epoch, restored->NextCast);
    checkpoint::Require(restored->Version && restored->Epoch && restored->NextCast,
                        "invalid checkpoint bridge identity");
    restored->ActiveProcess = checkpoint::ReadProgram(in);
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
