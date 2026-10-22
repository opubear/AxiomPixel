#pragma once
#include "PlayerInput.h"
#include "SimulationCoordinator.h"
#include "CommittedWorldView.h"
#include "Actions/ActionValues.h"
#include "FormulaRuntime.h"
#include "Observation/ObservationProjector.h"
#include "Observation/DomainFactBuffer.h"
#include "Persistence/SessionStore.h"
#include "World/World.h"
#include <optional>
#include <string>

namespace pixel {
// Native callers are host assembly credentials, never fields in an external payload.
enum class NativeCaller { Input, Debug, Driver };

namespace testing { struct SessionAccess; }
class GamePresentation;

class GameSession {
  public:
    static constexpr uint32_t WorldWidth = 1024;
    static constexpr uint32_t WorldHeight = 1024;
    static constexpr uint32_t TerrainHeight = 512;
    static constexpr uint32_t DefaultWindStartHeight = 600;
    // CPU fixtures remain finite; the application explicitly enables persistence.
    explicit GameSession(bool persistent = false, WorldGenerationSettings generation = {});
    static WorldGenerationSettings DefaultGeneration();
    uint64_t WorldEpoch() const { return Epoch; }
    api::ProgramRef AppliedProgram() const { return {1, Formulas.Version()}; }
    api::CallerSessionId CallerSession(NativeCaller caller) const;
    api::ActionReceipt Submit(api::CallerSessionId caller, const api::ActionRequest &request);
    api::ActionReceipt Execute(NativeCaller caller, api::Action action);
    const api::ActionReceipt *LastAction(NativeCaller caller) const;
    api::ControlReceipt Control(api::CallerSessionId caller, const api::SessionControl &control);
    api::ControlReceipt Control(NativeCaller caller, const api::SessionControl &control) {
        return Control(CallerSession(caller), control);
    }
    ApplyProgramResult ApplyProgram(api::CallerSessionId caller, const formula::Formula &description);
    ApplyProgramResult ApplyProgram(NativeCaller caller, const formula::Formula &description) {
        return ApplyProgram(CallerSession(caller), description);
    }
    void BeginFrame() { Formulas.BeginFrame(); }
    void ClearPlayerInput() {
        Simulation.ClearInput();
        if (!Simulation.HasPendingStep()) GameWorld.ClearPlayerInput();
    }
    const World &GetWorld() const { return GameWorld; }
    bool IsReady() const { return Ready; }
    bool CanEditWorld() const { return Ready && !GameWorld.GetEnvironment().IsWaitingForBudget(); }
    const FormulaRuntime &GetFormulaRuntime() const { return Formulas; }
    const std::string &GetFormulaError() const { return FormulaError; }
    const WorldGenerationSettings &GetGenerationSettings() const { return StreamConfiguration.Generation; }
    const WorldGenerationSettings &GetNextGenerationSettings() const { return NextGeneration; }
    const std::string &GetSaveError() const { return SaveError; }
    const api::ControlReceipt &LastPersistenceResult() const { return PersistenceResult; }
    void Tick(float duration, PlayerInput input);
    bool IsPaused() const { return Simulation.IsPaused(); }
    double GetSimulationTime() const { return Simulation.Time(); }
    double GetTimeRemainder() const { return Simulation.Remainder(); }
    api::CommitStamp CurrentCommit() const;
    // Native host access; external observer policies/leases belong to LLMView.
    api::ObservationResult Observe(NativeCaller observer, api::Rect scope);
    void CloseObservation(NativeCaller observer) { Observations.Close(size_t(observer)); }
    api::ObservationMetrics ObservationStats() const;
    api::ObservationServiceStatus ObservationService() const {
        return {CurrentCommit(), Ready, Simulation.HasPendingStep()};
    }
    uint64_t FactHighWatermark() const { return Facts.HighWatermark(); }
    api::FactReadResult ReadFacts(uint64_t epoch, uint64_t after, uint64_t high, uint32_t limit = 256) const {
        return Facts.Read(epoch, after, high, limit);
    }
    // Internal consumers track a bounded live projection, never a prepared step.
    std::shared_ptr<CommittedWorldView> CreateReadView();
    void RefreshReadView(CommittedWorldView &view, WorldRect bounds);
    WindStrategyKind GetSelectedWindStrategy() const { return WindKind; }
    const std::filesystem::path &GetSaveDirectory() const { return SaveDirectory; }

  private:
    friend class GamePresentation; // Snapshot ownership, residency and publication budgets only.
    friend struct testing::SessionAccess; // Fixture construction lives outside production targets.
    void Reset();
    bool TryReset();
    bool TryRecover();
    void Save();
    bool TrySave();
    bool SelectWindStrategy(WindStrategyKind kind);
    bool SetWindBoundary(int64_t y);
    struct CallerState {
        api::CallerSessionId Id = 0;
        uint64_t HighWatermark = 0;
        bool Debug = false;
        std::deque<api::ActionReceipt> Receipts;
    };
    uint64_t Epoch = 0;
    std::array<CallerState, 3> Callers;
    CallerState *FindCaller(api::CallerSessionId caller);
    api::Reason WriteRejection() const;
    api::ActionReceipt Dispatch(const api::Action &action, api::RequestKey request);
    api::Reason ActivateAt(WorldPosition target, api::RequestKey request);
    void RenewActionSessions();
    void RecordActionFacts(api::ActionReceipt &receipt, api::WindConfiguration beforeWind);
    void RecordFormulaFacts();
    World GameWorld{WorldWidth, WorldHeight};
    FormulaRuntime Formulas;
    std::array<double, 2> LastAim{1, 0};
    std::string FormulaError;
    std::optional<SessionStore> Store;
    SimulationCoordinator Simulation;
    ObservationProjector Observations;
    DomainFactBuffer Facts;
    std::vector<std::weak_ptr<CommittedWorldView>> ReadViews;
    uint64_t PublishedMapVersion = 0, PublishedWindVersion = 0;
    void PublishCommit();
    void CommitChange();
    void ServiceStreaming();
    void SynchronizeReadViews();
    WindStrategyKind WindKind = WindStrategyKind::Empty;
    std::filesystem::path SaveDirectory;
    WorldStreamSettings StreamConfiguration;
    WorldGenerationSettings NextGeneration;
    bool Ready = true;
    std::string SaveError;
    api::ControlReceipt PersistenceResult{api::ControlStatus::Applied};
    void InitializeScene();
    bool Load();
    void LoadDirectory(const std::filesystem::path &directory);
    SessionSaveData CaptureSaveData() const;
    void ReplacingWorld();
};
} // namespace pixel
