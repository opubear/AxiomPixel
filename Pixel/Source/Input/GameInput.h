#pragma once
#include "Gameplay/PlayerInput.h"
#include "Input/MousePointerState.h"
#include "SystemEnum.h"
#include <array>
#include <functional>

namespace pixel {
class GameSession;
class GameCamera;
class GamePresentation;
enum class ControlMode { Standard, DebugCamera };

// Runtime operations supplied by the application; input itself stays CPU-testable.
struct GameInputHost {
    std::function<void()> RequestQuit;
    std::function<void()> ToggleDebugUI;
    std::function<std::array<int, 2>()> GetFramebufferSize;
};

// Receives events after the engine's UI/focus filtering and invokes game actions.
// Referenced subsystems and callback targets must remain alive during input use.
class GameInput {
  public:
    GameInput(GameSession &session, GameCamera &camera, GamePresentation &presentation, GameInputHost host);
    void OnKeyEvent(Key key, int scancode, ButtonAction action, Mod mods);
    void OnMouseButtonEvent(MouseButton button, ButtonAction action, int mods, const MousePointerState &pointer);
    void OnMouseMove(float, float) {}
    void OnMouseMoveDelta(float, float) {}
    void ClearInputState();
    void SetControlMode(ControlMode mode);
    ControlMode GetControlMode() const { return Mode; }
    PlayerInput ConsumePlayerInput();
    // Called after simulation; standard following uses the player's new position.
    void UpdateCamera(float duration);
    void ResetView();
    static const char *ControlsDescription();

  private:
    GameSession &Session;
    GameCamera &Camera;
    GamePresentation &Presentation;
    GameInputHost Host;
    ControlMode Mode = ControlMode::Standard;
    std::array<bool, 4> Move{};
    bool JumpRequested = false;
    std::array<int, 2> Movement() const;
    void ResetGame();
};
} // namespace pixel
