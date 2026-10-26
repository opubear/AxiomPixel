#pragma once

#include "FormulaTypes.h"
#include "ProcessCarrier.h"
#include "Formula/Process/Program.h"
#include "Runtime/Gameplay/GameAPI/Facts.h"
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace pixel {
// Game-owned process/1 adapter. All collision queries precede effect commits;
// only successful world steps age instances.
class FormulaBridge {
  public:
    static constexpr size_t MaximumEffects = 256;
    FormulaBridge();
    ~FormulaBridge();
    // Host assembly at world replacement, before any instance can run.
    void BindPlayerInventory(items::Inventory &inventory, items::ContainerId container);
    void BindActors(const Actors &actors);
    FormulaBridge(const FormulaBridge &) = delete;
    FormulaBridge &operator=(const FormulaBridge &) = delete;
    void ApplyProcess(const formula::process::ProgramArtifact &program);
    api::ProgramRef AppliedProgram() const;
    bool CastCarrier(World &world, const ProcessCarrier &carrier,
                     std::optional<api::RequestKey> request = std::nullopt);
    bool CastProcess(World &world, const formula::process::ProgramArtifact &program, api::ProgramRef identity,
                     const ProcessCarrier &carrier, std::optional<api::RequestKey> request = std::nullopt);
    bool HasCarrier(ProcessCarrierKind kind, uint64_t subject) const;
    void CancelCarrier(World &world, ProcessCarrierKind kind, uint64_t subject);
    void WakeCarrier(ProcessCarrierKind kind, uint64_t subject);
    ProcessHostStats ProcessStats() const;
    std::vector<ProcessInspection> InspectProcesses() const;
    const formula::process::ProgramArtifact &ActiveProcess() const;
    const std::deque<ProcessReceipt> &ProcessReceipts() const;
    const std::deque<FormulaRuntimeDiagnostic> &ProcessDiagnostics() const;
    uint64_t Version() const;
    uint64_t LastActivationId() const;
    const std::string &LastError() const;
    void BeginFrame();
    bool SynchronizeActivity(World &world);
    bool Cast(World &world, WorldPosition anchor, double angleDegrees,
              std::optional<api::RequestKey> activationRequest = std::nullopt);
    void Step(World &world);
    std::span<const FormulaEffectRecord> LastEffects() const;
    void Clear(World &world);
    void OnWorldReplaced(World &world);
    const std::vector<FormulaProjectile> &Projectiles() const;
    const std::deque<FormulaRuntimeDiagnostic> &Diagnostics() const;
    FormulaRuntimeStats Stats() const;
    uint64_t WorldEpoch() const;
    // Complete committed process state. Restore is side-effect free and requires
    // an empty bridge; the session restores owned world sources separately.
    std::string CaptureState() const;
    void RestoreState(std::string_view encoded);

  private:
    struct State;
    std::unique_ptr<State> Data;
};
} // namespace pixel
