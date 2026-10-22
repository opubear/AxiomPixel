#include "../Support/TestAccess.h"
#include "Camera/GameCamera.h"
#include "Gameplay/GameSession.h"
#include "Input/GameInput.h"
#include "PixelRender/GamePresentation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Fixture {
    pixel::GameSession Session;
    pixel::GameCamera Camera;
    pixel::GamePresentation Presentation;
    std::array<int, 2> Framebuffer{800, 600};
    int QuitRequests = 0, PanelToggles = 0, EditorToggles = 0;
    pixel::GameInput Input{Session, Camera, Presentation, {
        [this] { ++QuitRequests; }, [this] { ++PanelToggles; }, [this] { ++EditorToggles; }, [this] { return Framebuffer; }}};
    Fixture() {
        pixel::testing::SessionAccess::World(Session).DisableWind();
        Input.ResetView();
    }
    void KeyEvent(Key key, ButtonAction action = ButtonAction::ACTION_PRESS) {
        Input.OnKeyEvent(key, 0, action, Mod{});
    }
};
void HeldInputAndModes() {
    Fixture f;
    f.KeyEvent(Key::KEY_D);
    f.KeyEvent(Key::KEY_SPACE);
    const auto first = f.Input.ConsumePlayerInput();
    Require(first.Horizontal == 1 && first.Jump, "press must move and jump");
    Require(!f.Input.ConsumePlayerInput().Jump, "jump must be consumed once");
    f.KeyEvent(Key::KEY_F3);
    Require(f.Input.GetControlMode() == pixel::ControlMode::DebugCamera, "F3 must change mode directly");
    const auto centre = f.Camera.GetCamera().GetPosition();
    f.KeyEvent(Key::KEY_D, ButtonAction::ACTION_REPEAT);
    f.Input.UpdateCamera(.1f);
    Require(f.Camera.GetCamera().GetPosition() == centre, "mode change must clear held input and suppress repeats");
    f.KeyEvent(Key::KEY_SPACE);
    Require(f.Presentation.Post.Values[1] == .35f, "debug space must execute tint change");
    f.KeyEvent(Key::KEY_D);
    f.Input.UpdateCamera(.1f);
    Require(f.Camera.GetCamera().GetPosition()[0] > centre[0] && f.Input.ConsumePlayerInput().Horizontal == 0,
            "debug input must pan camera without controlling the player");
    f.Input.ClearInputState();
    const auto cleared = f.Camera.GetCamera().GetPosition();
    f.KeyEvent(Key::KEY_D, ButtonAction::ACTION_REPEAT);
    f.Input.UpdateCamera(.1f);
    f.KeyEvent(Key::KEY_F3, ButtonAction::ACTION_REPEAT);
    Require(f.Camera.GetCamera().GetPosition() == cleared && f.Input.GetControlMode() == pixel::ControlMode::DebugCamera,
            "UI clear and repeats must not restore input or toggle mode");
    f.KeyEvent(Key::KEY_F3);
    Require(f.Camera.GetCamera().GetPosition() == cleared, "mode switch must preserve camera position");
    f.KeyEvent(Key::KEY_D);
    f.KeyEvent(Key::KEY_D, ButtonAction::ACTION_RELEASE);
    Require(f.Input.ConsumePlayerInput().Horizontal == 0, "release must stop movement");
    auto &world = pixel::testing::SessionAccess::World(f.Session);
    const auto playerX = world.GetPlayer()->X;
    world.SetPlayerInput(1, true);
    f.Input.ClearInputState();
    world.StepEnvironment(0);
    world.StepPlayer();
    Require(world.GetPlayer()->X == playerX && world.GetPlayer()->Y >= 508,
            "clear must also remove input already applied to world");
}
void CommandsAndReset() {
    Fixture f;
    f.KeyEvent(Key::KEY_F1);
    f.KeyEvent(Key::KEY_F1, ButtonAction::ACTION_REPEAT);
    f.KeyEvent(Key::KEY_F1, ButtonAction::ACTION_RELEASE);
    Require(f.Presentation.DisplayMode == GridDisplayMode::ForceField, "F1 must execute once per press");
    f.KeyEvent(Key::KEY_F2);
    f.KeyEvent(Key::KEY_F2, ButtonAction::ACTION_REPEAT);
    f.KeyEvent(Key::KEY_F2, ButtonAction::ACTION_RELEASE);
    Require(f.PanelToggles == 1 && f.EditorToggles == 0, "F2 must toggle only the debug panel");
    f.KeyEvent(Key::KEY_F4);
    f.KeyEvent(Key::KEY_F4, ButtonAction::ACTION_REPEAT);
    f.KeyEvent(Key::KEY_F4, ButtonAction::ACTION_RELEASE);
    Require(f.PanelToggles == 1 && f.EditorToggles == 1, "F4 must toggle only the formula editor once per press");
    f.KeyEvent(Key::KEY_ESCAPE);
    f.KeyEvent(Key::KEY_ESCAPE, ButtonAction::ACTION_RELEASE);
    Require(f.PanelToggles == 1 && f.QuitRequests == 1, "host actions must execute once per press");
    f.KeyEvent(Key::KEY_T);
    Require(f.Presentation.Post.Values[1] == .35f, "T must invoke presentation tint");
    const auto centre = f.Camera.GetCamera().GetPosition();
    f.KeyEvent(Key::KEY_EQUAL);
    Require(f.Camera.GetView().CellSizePixels[0] == 32 && f.Camera.GetCamera().GetPosition() == centre,
            "zoom must keep camera centre");
    f.KeyEvent(Key::KEY_MINUS);
    const auto view = f.Camera.GetView();
    f.KeyEvent(Key::KEY_PAGE_DOWN);
    Require(f.Camera.GetView().OriginPixels == view.OriginPixels, "standard mode must ignore region shortcut");
    f.KeyEvent(Key::KEY_F3);
    f.KeyEvent(Key::KEY_PAGE_DOWN);
    Require(f.Camera.GetView().WorldOriginCells[1] == view.WorldOriginCells[1] + pixel::storage::ChunkEdge,
            "debug mode must advance one world chunk while preserving local projection");
    f.KeyEvent(Key::KEY_D);
    f.Session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
    pixel::testing::SessionAccess::World(f.Session).RemoveDirt(20, 512);
    f.Camera.GetCamera().SetFollowRate(4);
    f.Framebuffer = {1600, 1200};
    f.KeyEvent(Key::KEY_R);
    Require(f.Session.IsPaused() && f.Session.GetSimulationTime() == 0 && pixel::testing::SessionAccess::World(f.Session).GetWindEndRow() == 424 &&
                pixel::testing::SessionAccess::World(f.Session).GetCell(20, 512).Type == pixel::CellTypes::Dirt,
            "R must reset simulation and terrain");
    Require(f.Input.GetControlMode() == pixel::ControlMode::DebugCamera && f.Presentation.DisplayMode == GridDisplayMode::ForceField,
            "R must preserve control and display modes");
    Require(f.Camera.GetFramebufferSize() == f.Framebuffer && f.Camera.GetView().CellSizePixels[0] == 16 &&
                f.Camera.GetCamera().GetFollowRate() == 4 && f.Presentation.Post.Values[1] == 1,
            "R must refresh viewport and reset zoom/tint while retaining follow rate");
    const auto resetCentre = f.Camera.GetCamera().GetPosition();
    f.Input.UpdateCamera(.1f);
    Require(f.Camera.GetCamera().GetPosition() == resetCentre, "R must clear held camera movement");
}
void MouseEditing() {
    Fixture f;
    auto &world = pixel::testing::SessionAccess::World(f.Session);
    f.Camera.GetCamera().SetPosition({100, 100});
    f.Camera.SetFramebufferSize({1600, 1200});
    const MousePointerState oldEvent{{400, 300}, {800, 600}, {800, 600}};
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_LEFT, ButtonAction::ACTION_PRESS, 0, oldEvent);
    Require(world.GetCell(100, 100).Type == pixel::CellTypes::Dirt && world.GetCell(75, 81).Type == pixel::CellTypes::Empty,
            "old-size click must edit the cell under its own viewport centre");
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_RELEASE, 0, oldEvent);
    Require(world.GetCell(100, 100).Type == pixel::CellTypes::Dirt, "mouse release must not edit world");
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_PRESS, 0, oldEvent);
    Require(world.GetCell(100, 100).Type == pixel::CellTypes::Dirt &&
                f.Session.GetFormulaRuntime().Projectiles().size() == 1,
            "right press must activate a formula without mining the target");
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_PRESS, int(Mod::MOD_SHIFT), oldEvent);
    Require(world.GetCell(100, 100).Type == pixel::CellTypes::Empty, "right press must remove dirt");
    auto invalid = oldEvent;
    invalid.FramebufferSize = {0, 0};
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_LEFT, ButtonAction::ACTION_PRESS, 0, invalid);
    invalid = oldEvent;
    invalid.Position = {-1, -1};
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_LEFT, ButtonAction::ACTION_PRESS, 0, invalid);
    Require(world.GetCell(100, 100).Type == pixel::CellTypes::Empty, "invalid/outside click must not edit world");
}

void FormulaInputAndContinuousAim() {
    Fixture f;
    auto &runtime = f.Session.GetFormulaRuntime();
    const MousePointerState centre{{400, 300}, {800, 600}, {800, 600}};
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_PRESS, 0, centre);
    Require(runtime.Projectiles().size() == 1, "paused right press must create the final bolt");
    const auto ball = runtime.Projectiles().front();
    const auto relative = ball.Position.RelativeTo({512, 510});
    Require(std::abs(relative[0] - 1.6) < 1e-9 && std::abs(relative[1]) < 1e-9,
            "zero-distance aim must use right-facing muzzle outside the player");
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_RELEASE, 0, centre);
    f.Session.Tick(.2f, {});
    Require(runtime.Projectiles().size() == 1 && runtime.Projectiles().front().Position.Local == ball.Position.Local,
            "release or pause moved/repeated the bolt");
    f.Session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    f.Session.Tick(.2f, {});
    const auto moved = runtime.Projectiles().front().Position.RelativeTo(ball.Position.Cell());
    const auto before = ball.Position.RelativeTo(ball.Position.Cell());
    Require(std::abs(moved[0] - before[0] - 1) < 1e-9, "single step must move the default bolt one cell");

    const pixel::WorldPosition distant{{int64_t{1} << 44, -(int64_t{1} << 44)}, {.125, .75}};
    f.Camera.GetCamera().SetWorldPosition(distant);
    f.Camera.SetFramebufferSize({1600, 1200});
    const auto target = f.Camera.PickWorldPosition({{400.25, 300.5}, {800, 600}, {800, 600}});
    Require(target && target->Chunk == distant.Chunk &&
                std::abs(target->Local[0] - .140625) < 1e-12 && std::abs(target->Local[1] - .78125) < 1e-12,
            "continuous old-size event lost distant fractional aim");
    Require(!f.Camera.PickWorldPosition({{800, 300}, {800, 600}, {800, 600}}), "outside continuous aim accepted");
}
} // namespace
int main() {
    try {
        HeldInputAndModes();
        CommandsAndReset();
        MouseEditing();
        FormulaInputAndContinuousAim();
        std::cout << "Pixel input behavior passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
