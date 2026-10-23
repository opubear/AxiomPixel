#pragma once
#include "Actors/Actors.h"
#include "Devices/Devices.h"
#include "SimulationCoordinator.h"
#include "CommittedWorldView.h"
#include "Actions/ActionValues.h"
#include "FormulaBridge/FormulaBridge.h"
#include "Observation/ObservationProjector.h"
#include "Observation/DomainFactBuffer.h"
#include "Knowledge/Knowledge.h"
#include "GameAPI/ObservationSource.h"
#include "Persistence/SessionStore.h"
#include "World/World.h"
#include <optional>
#include <string>

namespace pixel {
struct GameplaySaveData;
struct CheckpointMetrics {
    double CaptureMicroseconds = 0, WriteMicroseconds = 0, TotalMicroseconds = 0;
    uint64_t PayloadBytes = 0;
};
// Native callers are host assembly credentials, never fields in an external payload.
enum class NativeCaller { Input, Debug, Driver };

namespace testing { struct SessionAccess; }
class GamePresentation;

class GameSession : public api::ObservationSource {
  public:
    static constexpr uint32_t WorldWidth = 1024;
    static constexpr uint32_t WorldHeight = 1024;
    static constexpr uint32_t TerrainHeight = 512;
    static constexpr uint32_t DefaultWindStartHeight = 600;
    // CPU fixtures remain finite; the application explicitly enables persistence.
    explicit GameSession(bool persistent = false, WorldGenerationSettings generation = {});
    static WorldGenerationSettings DefaultGeneration();
    uint64_t WorldEpoch() const { return Epoch; }
    api::ProgramRef AppliedProgram() const { return Formulas.AppliedProgram(); }
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
    ApplyProgramResult ApplyProcess(NativeCaller caller, const formula::process::ProgramArtifact &program);
    api::ControlReceipt StartProcessFixture(NativeCaller caller, const ProcessCarrier &carrier);
    api::ControlReceipt CancelProcessCarrier(NativeCaller caller, ProcessCarrierKind kind, uint64_t subject);
    api::ControlReceipt WakeProcessCarrier(NativeCaller caller, ProcessCarrierKind kind, uint64_t subject);
    void BeginFrame() { Formulas.BeginFrame(); }
    void ClearPlayerInput() { Simulation.ClearInput(); }
    const World &GetWorld() const { return GameWorld; }
    const Actors &GetActors() const { return Characters; }
    const Devices &GetDevices() const { return Machines; }
    const items::Inventory &GetInventory() const { return Inventory; }
    items::ContainerId PlayerInventory() const { return PlayerBag; }
    const Character *GetPlayer() const { return Characters.Player(); }
    bool IsReady() const { return Ready; }
    bool CanEditWorld() const { return Ready && !GameWorld.GetEnvironment().IsWaitingForBudget(); }
    const FormulaBridge &GetFormulaRuntime() const { return Formulas; }
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
    api::ObservationResult CaptureObservation(api::Rect scope) override;
    void CloseObservation(NativeCaller observer) { Observations.Close(size_t(observer)); }
    api::ObservationMetrics ObservationStats() const;
    knowledge::SaveData CaptureKnowledgeData() const { return Knowledge.Export(); }
    api::ObservationServiceStatus ObservationService() const override {
        return {CurrentCommit(), Ready, Simulation.HasPendingStep()};
    }
    uint64_t FactHighWatermark() const { return Facts.HighWatermark(); }
    api::FactReadResult ReadFacts(uint64_t epoch, uint64_t after, uint64_t high, uint32_t limit = 256) const override {
        return Facts.Read(epoch, after, high, limit);
    }
    // Internal consumers track a bounded live projection, never a prepared step.
    std::shared_ptr<CommittedWorldView> CreateReadView();
    void RefreshReadView(CommittedWorldView &view, WorldRect bounds);
    WindStrategyKind GetSelectedWindStrategy() const { return WindKind; }
    const std::filesystem::path &GetSaveDirectory() const { return SaveDirectory; }
    const CheckpointMetrics &LastCheckpointMetrics() const { return SaveMetrics; }

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
    api::FactId RecordFact(const api::CommitStamp &stamp, api::FactCause cause, api::FactPayload payload);
    void RecordFormulaFacts();
    void RecordFormulaFacts(std::span<const FormulaEffectRecord> effects);
    api::ActionReceipt PlaceDevice(api::Cell target);
    api::ActionReceipt ChangeDevice(DeviceRef device, const api::Action &action);
    DeviceState DeviceEnvironment(const DeviceSnapshot &device) const;
    std::vector<api::DeviceChanged> SynchronizeDeviceActivity();
    void StepDevices(std::vector<FormulaEffectRecord> &effects, std::vector<api::DeviceChanged> &changes);
    void InitializeForceGarden();
    void StartGardenCrystal(std::vector<FormulaEffectRecord> &effects);
    void StepForceGarden(std::vector<FormulaEffectRecord> &effects);
    GardenSample SampleGarden(int64_t x, uint32_t growth) const;
    ForceGardenState Garden;
    bool CreateGardenOnReset = false;
    std::vector<api::DeviceChanged> StopDevices();
    World GameWorld{WorldWidth, WorldHeight};
    Actors Characters;
    Devices Machines;
    items::Inventory Inventory;
    items::ContainerId PlayerBag;
    FormulaBridge Formulas;
    std::array<double, 2> LastAim{1, 0};
    std::string FormulaError;
    std::optional<SessionStore> Store;
    SimulationCoordinator Simulation;
    ObservationProjector Observations;
    api::GameplayObservation GameplayValues;
    void PublishGameplayObservation();
    DomainFactBuffer Facts;
    knowledge::Journal Knowledge;
    std::vector<std::weak_ptr<CommittedWorldView>> ReadViews;
    uint64_t PublishedMapVersion = 0, PublishedWindVersion = 0, PublishedActorVersion = 0, PublishedDeviceVersion = 0;
    uint64_t PublishedFieldSourceVersion = 0;
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
    GameplaySaveData CaptureGameplayData() const;
    void RestoreGameplayData(const GameplaySaveData &data);
    CheckpointMetrics SaveMetrics;
    void ReplacingWorld();
};
} // namespace pixel
