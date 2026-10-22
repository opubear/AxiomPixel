#pragma once
#include "PlayerInput.h"
#include "Actions/ActionValues.h"
#include "FormulaRuntime.h"
#include "Persistence/SessionStore.h"
#include "World/World.h"
#include <optional>
#include <string>

namespace pixel {
// Native callers are host assembly credentials, never fields in an external payload.
enum class NativeCaller { Input, Debug, Driver };

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
    void ClearPlayerInput() { GameWorld.ClearPlayerInput(); }
    World &GetWorld() { return GameWorld; }
    const World &GetWorld() const { return GameWorld; }
    void Reset();
    bool TryReset();
    bool TryRecover();
    bool IsReady() const { return Ready; }
    bool CanEditWorld() const { return Ready && !GameWorld.GetEnvironment().IsWaitingForBudget(); }
    FormulaRuntime &GetFormulaRuntime() { return Formulas; }
    const FormulaRuntime &GetFormulaRuntime() const { return Formulas; }
    bool CastFormulaAt(WorldPosition target);
    const std::string &GetFormulaError() const { return FormulaError; }
    const WorldGenerationSettings &GetGenerationSettings() const { return StreamConfiguration.Generation; }
    const WorldGenerationSettings &GetNextGenerationSettings() const { return NextGeneration; }
    void SetNextGenerator(WorldGeneratorKind kind) { NextGeneration.Kind = kind; }
    void SetNextWorldSeed(uint64_t seed) { NextGeneration.Seed = seed; }
    void Save();
    bool TrySave();
    const std::string &GetSaveError() const { return SaveError; }
    void Tick(float duration, PlayerInput input);
    bool IsPaused() const { return Paused; }
    void SetPaused(bool paused) { Paused = paused; }
    void RequestStep() { StepRequested = true; }
    double GetSimulationTime() const { return SimulationTime; }
    WindStrategyKind GetSelectedWindStrategy() const { return WindKind; }
    bool SelectWindStrategy(WindStrategyKind kind);
    bool SetWindStartHeight(int height);
    bool SetWindBoundary(int64_t y);
    const std::filesystem::path &GetSaveDirectory() const { return SaveDirectory; }

  private:
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
    api::ActionReceipt Dispatch(const api::Action &action);
    api::Reason ActivateAt(WorldPosition target);
    void RenewActionSessions();
    World GameWorld{WorldWidth, WorldHeight};
    FormulaRuntime Formulas;
    std::array<double, 2> LastAim{1, 0};
    std::string FormulaError;
    std::optional<SessionStore> Store;
    bool Paused = true;
    bool StepRequested = false;
    double SimulationTime = 0;
    WindStrategyKind WindKind = WindStrategyKind::Empty;
    std::filesystem::path SaveDirectory;
    WorldStreamSettings StreamConfiguration;
    WorldGenerationSettings NextGeneration;
    bool Ready = true;
    std::string SaveError;
    void InitializeScene();
    bool Load();
    void LoadDirectory(const std::filesystem::path &directory);
    SessionSaveData CaptureSaveData() const;
    void ReplacingWorld();
};
} // namespace pixel
