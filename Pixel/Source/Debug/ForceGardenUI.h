#pragma once
#include <string>

namespace pixel {
class GameSession;
class GamePresentation;
void DrawForceGardenUI(GameSession &session, const GamePresentation &presentation, std::string &receipt);
} // namespace pixel
