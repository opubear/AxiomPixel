#pragma once
#include "Runtime/Gameplay/GameAPI/WorldWillState.h"
#include <array>
#include <string>

namespace pixel {
class GameSession;
class GamePresentation;
namespace will { class WorldWill; }
struct ForceGardenUIState {
    std::array<char, api::WorldWillMemory::PrayerBytes + 1> Prayer{};
    std::string Receipt;
};
void DrawForceGardenUI(GameSession &session, const GamePresentation &presentation, will::WorldWill &worldWill,
                      ForceGardenUIState &state);
} // namespace pixel
