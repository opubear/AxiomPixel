#pragma once
#include "Runtime/Gameplay/GameAPI/WorldWillState.h"
#include "Runtime/Gameplay/Items/Item.h"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace pixel {
class GameSession;
class GamePresentation;
namespace will { class WorldWill; }
struct ForceGardenUIState {
    std::array<char, api::WorldWillMemory::PrayerBytes + 1> Prayer{};
    std::string Receipt;
    std::string QueuedOperation;
    std::vector<api::RequestKey> QueuedActions;
};
void DrawForceGardenUI(GameSession &session, const GamePresentation &presentation, will::WorldWill &worldWill,
                      ForceGardenUIState &state, std::optional<items::ItemRef> held);
} // namespace pixel
