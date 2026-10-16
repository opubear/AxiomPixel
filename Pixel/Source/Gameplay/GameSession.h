#pragma once
#include "World/World.h"
#include "PlayerInput.h"

namespace pixel {
// Game defaults and simulation controls, usable without a window or renderer.
class GameSession {
  public:
    static constexpr uint32_t WorldWidth = 1024;
    static constexpr uint32_t WorldHeight = 1024;
    static constexpr uint32_t TerrainHeight = 512;
    static constexpr uint32_t DefaultWindStartHeight = 600;
    GameSession();
    World &GetWorld() { return GameWorld; }
    const World &GetWorld() const { return GameWorld; }
    void Reset();
    void Tick(float duration, PlayerInput input);
    bool IsPaused() const { return Paused; }
    void SetPaused(bool paused) { Paused = paused; }
    void RequestStep() { StepRequested = true; }
    double GetSimulationTime() const { return SimulationTime; }
    WindStrategyKind GetSelectedWindStrategy() const { return WindKind; }
    void SelectWindStrategy(WindStrategyKind kind);
    void SetWindStartHeight(int height);

  private:
    World GameWorld{WorldWidth, WorldHeight};
    bool Paused = true;
    bool StepRequested = false;
    double SimulationTime = 0;
    WindStrategyKind WindKind = WindStrategyKind::Empty;
    WindSettings SavedWindSettings;
};
} // namespace pixel
