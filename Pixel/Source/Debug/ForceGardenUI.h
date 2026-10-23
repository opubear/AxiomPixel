#pragma once
#include <string>

namespace pixel {
class GameSession;
class GamePresentation;
namespace will { class WorldWill; }
void DrawForceGardenUI(GameSession &session, const GamePresentation &presentation, will::WorldWill &worldWill, std::string &receipt);
} // namespace pixel
