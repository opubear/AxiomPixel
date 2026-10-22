#include "GameInput.h"
#include "Camera/GameCamera.h"
#include "Gameplay/GameSession.h"
#include "PixelRender/GamePresentation.h"
#include <cmath>
#include <stdexcept>
#include <utility>

namespace pixel {
GameInput::GameInput(GameSession &session, GameCamera &camera, GamePresentation &presentation, GameInputHost host)
    : Session(session), Camera(camera), Presentation(presentation), Host(std::move(host)) {
    if (!Host.RequestQuit || !Host.ToggleDebugUI || !Host.ToggleFormulaEditor || !Host.GetFramebufferSize)
        throw std::invalid_argument("game input requires quit, debug UI, formula editor and framebuffer callbacks");
}
void GameInput::ClearInputState() {
    Move.fill(false);
    JumpRequested = false;
    Session.ClearPlayerInput();
}
void GameInput::SetControlMode(ControlMode mode) {
    if (mode == Mode)
        return;
    Mode = mode;
    ClearInputState();
    // Keep camera position; standard mode eases back after simulation.
}
std::array<int, 2> GameInput::Movement() const {
    return {int(Move[3]) - int(Move[1]), int(Move[2]) - int(Move[0])};
}
PlayerInput GameInput::ConsumePlayerInput() {
    const PlayerInput input{Mode == ControlMode::Standard ? Movement()[0] : 0,
                            Mode == ControlMode::Standard && JumpRequested};
    JumpRequested = false;
    return input;
}
void GameInput::UpdateCamera(float duration) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    if (Mode == ControlMode::Standard)
        Camera.FollowPlayer(Session.GetPlayer(), duration);
    else
        Camera.Pan(Movement(), duration);
}
void GameInput::ResetView() {
    Camera.SetFramebufferSize(Host.GetFramebufferSize());
    Camera.Reset(Session.GetWorld().GetSimulationBounds(), Session.GetPlayer());
    Presentation.Post = {};
}
void GameInput::ResetGame() {
    if (!Session.Control(NativeCaller::Input, api::ResetWorld{}).Succeeded()) {
        ClearInputState();
        return;
    }
    ClearInputState();
    ResetView();
}
void GameInput::OnMouseButtonEvent(MouseButton button, ButtonAction action, int mods, const MousePointerState &pointer) {
    if (action != ButtonAction::ACTION_PRESS || (button != MouseButton::MOUSE_BUTTON_LEFT && button != MouseButton::MOUSE_BUTTON_RIGHT))
        return;
    if (button == MouseButton::MOUSE_BUTTON_RIGHT && !(mods & int(Mod::MOD_SHIFT))) {
        if (const auto target = Camera.PickWorldPosition(pointer))
            Session.Execute(NativeCaller::Input, api::ActivateProgram{Session.AppliedProgram(), ActionPosition(*target)});
        return;
    }
    const auto cell = Camera.PickCell(pointer);
    if (!cell)
        return;
    if (button == MouseButton::MOUSE_BUTTON_LEFT)
        Session.Execute(NativeCaller::Input, api::PlaceMaterial{{(*cell)[0], (*cell)[1]}});
    else
        Session.Execute(NativeCaller::Input, api::MineRegion{{(*cell)[0], (*cell)[1]}, {}});
}
void GameInput::OnKeyEvent(Key key, int, ButtonAction action, Mod) {
    const std::array keys{Key::KEY_W, Key::KEY_A, Key::KEY_S, Key::KEY_D};
    // Repeats must not reactivate a held key after mode/UI input was cleared.
    for (size_t i = 0; i < keys.size(); ++i)
        if (key == keys[i] && (action == ButtonAction::ACTION_PRESS || action == ButtonAction::ACTION_RELEASE))
            Move[i] = action == ButtonAction::ACTION_PRESS;
    if (action != ButtonAction::ACTION_PRESS)
        return;
    switch (key) {
    case Key::KEY_ESCAPE: Host.RequestQuit(); break;
    case Key::KEY_SPACE:
        if (Mode == ControlMode::Standard)
            JumpRequested = true;
        else
            Presentation.ToggleTint();
        break;
    case Key::KEY_T: Presentation.ToggleTint(); break;
    case Key::KEY_F1: Presentation.ToggleFieldView(); break;
    case Key::KEY_F2: Host.ToggleDebugUI(); break;
    case Key::KEY_F3:
        SetControlMode(Mode == ControlMode::Standard ? ControlMode::DebugCamera : ControlMode::Standard);
        break;
    case Key::KEY_F4: Host.ToggleFormulaEditor(); break;
    case Key::KEY_R: ResetGame(); break;
    case Key::KEY_PAGE_DOWN:
        if (Mode == ControlMode::DebugCamera)
            Camera.NextRegion();
        break;
    case Key::KEY_EQUAL: Camera.Zoom(true); break;
    case Key::KEY_MINUS: Camera.Zoom(false); break;
    default: break;
    }
}
const char *GameInput::ControlsDescription() {
    return "Pixel: left click place dirt; right click activate formula; Shift+right click mine; A/D move; Space jump/swim up; F3 debug camera (WASD pan); +/- zoom; F1 force-field view; F2 Debug UI; F4 Formula Editor; T tint; PageDown next region; R new world with selected generator; Escape quit";
}
} // namespace pixel
