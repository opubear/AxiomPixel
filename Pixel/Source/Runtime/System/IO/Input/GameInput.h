#pragma once
#include "Runtime/Gameplay/Actors/PlayerInput.h"
#include "Input/MousePointerState.h"
#include "Runtime/Gameplay/UI/InventoryUI.h"
#include "SystemEnum.h"
#include <array>
#include <functional>

namespace pixel {
class GameSession;
class GameCamera;
class PresentationControl;
enum class ElementBrush { Disabled, Water, Steam, Fire };
enum class ControlMode { Standard, DebugCamera };

// Runtime operations supplied by the application; input itself stays CPU-testable.
struct GameInputHost {
    std::function<void()> RequestQuit;
    std::function<void()> ToggleDebugUI;
    std::function<void()> ToggleFormulaEditor;
    std::function<std::array<int, 2>()> GetFramebufferSize;
};

// Receives events after the engine's UI/focus filtering and invokes game actions.
// Referenced subsystems and callback targets must remain alive during input use.
class GameInput {
  public:
    GameInput(GameSession &session, GameCamera &camera, PresentationControl &presentation, ui::InventoryUI &ui, GameInputHost host);
    void OnKeyEvent(Key key, int scancode, ButtonAction action, Mod mods);
    void OnMouseButtonEvent(MouseButton button, ButtonAction action, int mods, const MousePointerState &pointer);
    void OnMouseMove(float, float) {}
    void OnMouseMoveDelta(float, float) {}
    void ClearInputState();
    void SetControlMode(ControlMode mode);
    ControlMode GetControlMode() const { return Mode; }
    bool IsDevicePlacementEnabled() const { return DevicePlacementMode; }
    void SetDevicePlacementEnabled(bool enabled) { DevicePlacementMode = enabled; if (enabled) Brush = ElementBrush::Disabled; }
    ElementBrush GetElementBrush() const { return Brush; }
    void SetElementBrush(ElementBrush brush) { Brush = brush; DevicePlacementMode = false; }
    PlayerInput ConsumePlayerInput();
    // Called after simulation; standard following uses the player's new position.
    void UpdateCamera(float duration);
    void ResetView();
    static const char *ControlsDescription();

  private:
    GameSession &Session;
    GameCamera &Camera;
    PresentationControl &Presentation;
    ui::InventoryUI &UI;
    GameInputHost Host;
    ControlMode Mode = ControlMode::Standard;
    std::array<bool, 4> Move{};
    bool JumpRequested = false;
    bool DevicePlacementMode = false;
    ElementBrush Brush = ElementBrush::Disabled;
    std::array<int, 2> Movement() const;
    void ResetGame();
};
} // namespace pixel
