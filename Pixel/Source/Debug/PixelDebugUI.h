#pragma once
#include "Input/GameInput.h"
#include <optional>

namespace pixel {
class GameSession;
class GameCamera;
class GamePresentation;
namespace performance { class GameProfiler; }

// Owns panel visibility only; simulation and presentation remain authoritative.
class PixelDebugUI {
  public:
    void Toggle() { Visible = !Visible; }
    // Mode changes return to GameInput through the application's lifecycle adapter.
    std::optional<ControlMode> Draw(GameSession &session, GameCamera &camera,
                                    GamePresentation &presentation, ControlMode mode,
                                    performance::GameProfiler &performance);
  private:
    bool Visible = true;
};
} // namespace pixel
