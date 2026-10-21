#pragma once
#include "PlayerInput.h"
#include "FormulaRuntime.h"
#include "Persistence/SessionStore.h"
#include "World/World.h"
#include <optional>
#include <string>

namespace pixel {
class GameSession {
  public:
    static constexpr uint32_t WorldWidth = 1024;
    static constexpr uint32_t WorldHeight = 1024;
    static constexpr uint32_t TerrainHeight = 512;
    static constexpr uint32_t DefaultWindStartHeight = 600;
    // CPU fixtures remain finite; the application explicitly enables persistence.
    explicit GameSession(bool persistent = false, WorldGenerationSettings generation = {});
    static WorldGenerationSettings DefaultGeneration();
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
