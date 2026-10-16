#include "GameSession.h"
#include "Performance/FrameProfiler.h"
#include <algorithm>
#include <cmath>

namespace pixel {
static_assert(GameSession::TerrainHeight < GameSession::DefaultWindStartHeight &&
              GameSession::DefaultWindStartHeight <= GameSession::WorldHeight);
GameSession::GameSession() { Reset(); }
void GameSession::Reset() {
    GameWorld.GenerateTerrain(WorldHeight - TerrainHeight);
    GameWorld.SpawnPlayer(WorldWidth / 2 - Character::Width / 2, WorldHeight - TerrainHeight - Character::Height);
    WindKind = WindStrategyKind::Empty;
    Paused = true;
    StepRequested = false;
    SimulationTime = 0;
    SavedWindSettings = {};
    WindStrategySettings settings;
    settings.Kind = WindKind;
    GameWorld.ConfigureWindStrategy(WorldHeight - DefaultWindStartHeight, settings);
}
void GameSession::Tick(float duration, PlayerInput input) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    performance::FrameProfiler::Count(performance::Counter::SimulationInputMicroseconds,
                                     uint64_t(std::min(duration, .25f) * 1e6));
    GameWorld.SetPlayerInput(input.Horizontal, input.Jump);
    if (!Paused || StepRequested) {
        const float step = StepRequested ? 1.f / 60 : std::min(duration, .25f);
        GameWorld.Tick(step);
        // Both wind shader phases have a two-second period; wrapping stays continuous.
        SimulationTime = std::fmod(SimulationTime + step, 4096.0);
    }
    StepRequested = false;
}
void GameSession::SetWindStartHeight(int height) {
    const auto worldHeight = GameWorld.ReadState().Height;
    const auto endRow = worldHeight - uint32_t(std::clamp(height, 0, int(worldHeight)));
    if (GameWorld.GetWind().IsEnabled())
        SavedWindSettings = GameWorld.GetWind().GetSettings();
    if (endRow == 0)
        GameWorld.DisableWind();
    else if (endRow != GameWorld.GetWindEndRow() || WindKind != GameWorld.GetWindStrategyKind()) {
        WindStrategySettings settings;
        settings.Kind = WindKind;
        settings.Coarse = SavedWindSettings;
        GameWorld.ConfigureWindStrategy(endRow, settings);
    }
}
void GameSession::SelectWindStrategy(WindStrategyKind kind) {
    WindKind = kind;
    if (kind == WindStrategyKind::Reference || kind == WindStrategyKind::Adaptive || kind == WindStrategyKind::Lbm)
        Paused = true;
    StepRequested = false;
    SetWindStartHeight(int(GameWorld.ReadState().Height - GameWorld.GetWindEndRow()));
}
} // namespace pixel
