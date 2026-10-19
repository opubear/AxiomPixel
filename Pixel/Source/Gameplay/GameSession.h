#pragma once
#include "PlayerInput.h"
#include "World/World.h"
#include <string>

namespace pixel {
class GameSession {
  public:
    static constexpr uint32_t WorldWidth = 1024;
    static constexpr uint32_t WorldHeight = 1024;
    static constexpr uint32_t TerrainHeight = 512;
    static constexpr uint32_t DefaultWindStartHeight = 600;
    // CPU fixtures remain finite; the application explicitly enables persistence.
    explicit GameSession(bool persistent = false);
    World &GetWorld() { return GameWorld; }
    const World &GetWorld() const { return GameWorld; }
    void Reset();
    void Save();
    bool TrySave();
    const std::string &GetSaveError() const { return SaveError; }
    void Tick(float duration, PlayerInput input);
    bool IsPaused() const { return Paused; }
    void SetPaused(bool paused) { Paused = paused; }
    void RequestStep() { StepRequested = true; }
    double GetSimulationTime() const { return SimulationTime; }
    WindStrategyKind GetSelectedWindStrategy() const { return WindKind; }
    void SelectWindStrategy(WindStrategyKind kind);
    void SetWindStartHeight(int height);
    void SetWindBoundary(int64_t y);
    const std::filesystem::path &GetSaveDirectory() const { return SaveDirectory; }

  private:
    World GameWorld{WorldWidth, WorldHeight};
    bool Persistent = false;
    bool Paused = true;
    bool StepRequested = false;
    double SimulationTime = 0;
    WindStrategyKind WindKind = WindStrategyKind::Empty;
    std::filesystem::path SaveRoot, SaveDirectory;
    WorldStreamSettings StreamConfiguration;
    std::string SaveError;
    void InitializeScene();
    bool Load();
    void WriteMetadata();
};
} // namespace pixel
