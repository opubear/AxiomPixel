#pragma once
#include "Runtime/Gameplay/Actors/Actors.h"
#include "Runtime/Gameplay/Devices/Devices.h"
#include "Runtime/Gameplay/SimulationCoordinator.h"
#include "Runtime/Gameplay/CommittedWorldView.h"
#include "Runtime/Gameplay/Actions/ActionValues.h"
#include "Runtime/Gameplay/FormulaBridge/FormulaBridge.h"
#include "Runtime/Gameplay/Observation/ObservationProjector.h"
#include "Runtime/Gameplay/Observation/DomainFactBuffer.h"
#include "Runtime/Gameplay/Knowledge/Knowledge.h"
#include "Runtime/Gameplay/Authority/Authority.h"
#include "Runtime/Gameplay/GameAPI/WorldWillHost.h"
#include "Runtime/Gameplay/Persistence/SessionStore.h"
#include "Runtime/Simulation/World/World.h"
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

class GameSession : public api::WorldWillHost {
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
    // Trusted host binding for the future model adapter; never decoded from JSON.
    api::CallerSessionId WorldWillSession() const { return Callers[3].Id; }
    api::ControlReceipt SetWorldWillCapabilities(NativeCaller caller, authority::Capabilities capabilities);
    api::ObservationAuthorization AuthorizeObservation(uint64_t subject, api::Rect scope) const override;
    const authority::Control &GetAuthority() const { return Authority; }
    api::WorldWillHostStatus WorldWillStatus() const override;
    uint64_t BeginPrayer(const api::CommitStamp &observed, api::Rect scope, std::string prayer) override;
    void ResolvePrayer(uint64_t id, api::DecisionState state, std::string explanation,
                       const std::optional<api::ActionReceipt> &receipt) override;
    api::ActionReceipt Submit(api::CallerSessionId caller, const api::ActionRequest &request) override;
    api::ActionReceipt Execute(NativeCaller caller, api::Action action);
    const api::ActionReceipt *LastAction(NativeCaller caller) const;
    api::ControlReceipt Control(api::CallerSessionId caller, const api::SessionControl &control);
    api::ControlReceipt Control(NativeCaller caller, const api::SessionControl &control) {
        return Control(CallerSession(caller), control);
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
    const Body *GetPlayer() const { return Characters.Player(); }
    bool IsReady() const { return Ready; }
    bool CanEditWorld() const { return Ready && !Paused && !GameWorld.GetEnvironment().IsWaitingForBudget(); }
    const FormulaBridge &GetFormulaRuntime() const { return Formulas; }
    const std::string &GetFormulaError() const { return FormulaError; }
    const WorldGenerationSettings &GetGenerationSettings() const { return StreamConfiguration.Generation; }
    const WorldGenerationSettings &GetNextGenerationSettings() const { return NextGeneration; }
    const std::string &GetSaveError() const { return SaveError; }
    const api::ControlReceipt &LastPersistenceResult() const { return PersistenceResult; }
    void Tick(float duration, PlayerInput input);
    bool IsPaused() const { return Paused; }
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
    std::array<CallerState, 4> Callers;
    CallerState *FindCaller(api::CallerSessionId caller);
    api::Reason WriteRejection() const;
    api::Reason PrepareWorldMutation();
    api::ActionReceipt Dispatch(const api::Action &action, api::RequestKey request);
    api::Reason ActivateAt(WorldPosition target, api::RequestKey request);
    void RenewActionSessions();
    api::Reason AuthorizeAction(api::CallerSessionId caller, const api::Action &action) const;
    api::ActionReceipt InstallIntervention(const api::InstallIntervention &action, api::RequestKey request);
    api::ActionReceipt InterveneAtDevice(const api::InterveneAtDevice &action, api::RequestKey request);
    api::ActionReceipt SetDeviceProtection(const api::SetDeviceProtection &action);
    std::vector<api::FactPayload> ReconcileAuthority();
    void RecordAuthorityFacts(std::span<const api::FactPayload> changes);
    authority::Control Authority;
    uint64_t NextIntervention = 1;
    std::vector<authority::Intervention> Interventions;
    api::WorldWillMemory WillMemory;
    void RecordActionFacts(api::ActionReceipt &receipt, api::WindConfiguration beforeWind);
    api::FactId RecordFact(const api::CommitStamp &stamp, api::FactCause cause, api::FactPayload payload);
    void RecordFormulaFacts();
    void RecordFormulaFacts(std::span<const FormulaEffectRecord> effects);
    api::ActionReceipt PlaceDevice(api::Cell target);
    api::ActionReceipt ChangeDevice(DeviceRef device, const api::Action &action);
    DeviceState DeviceEnvironment(const DeviceSnapshot &device) const;
    std::vector<api::DeviceChanged> SynchronizeDeviceActivity();
    void StepDevices(std::vector<FormulaEffectRecord> &effects, std::vector<api::DeviceChanged> &changes);
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
    bool Paused = false;
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
    void RestoreGameplayData(const GameplaySaveData &data, const std::string &worldId);
    void RestoreSocialData(const GameplaySaveData &data, const std::string &worldId);
    CheckpointMetrics SaveMetrics;
    void ReplacingWorld();
};
} // namespace pixel
