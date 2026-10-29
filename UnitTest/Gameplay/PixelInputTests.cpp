#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/System/IO/Input/GameInput.h"
#include "Runtime/Render/GamePresentation.h"
#include <algorithm>
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
    pixel::ui::InventoryUI UI;
    std::array<int, 2> Framebuffer{800, 600};
    int QuitRequests = 0, PanelToggles = 0, EditorToggles = 0;
    pixel::GameInput Input{Session, Camera, Presentation, UI, {
        [this] { ++QuitRequests; }, [this] { ++PanelToggles; }, [this] { ++EditorToggles; }, [this] { return Framebuffer; }}};
    Fixture() {
        pixel::testing::SessionAccess::World(Session).DisableWind();
        Input.ResetView();
    }
    void KeyEvent(Key key, ButtonAction action = ButtonAction::ACTION_PRESS, Mod mods = Mod{}) {
        Input.OnKeyEvent(key, 0, action, mods);
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
    const auto position = f.Session.GetPlayer()->GetWorldPosition();
    // A sub-step frame queues jump intent before any physics step can consume it.
    f.Session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
    f.Session.Tick(.001f, {1, true});
    f.Input.ClearInputState();
    f.Session.Tick(1.f / 60, f.Input.ConsumePlayerInput());
    const auto after = f.Session.GetPlayer()->GetWorldPosition().RelativeTo(position.Cell());
    const auto before = position.RelativeTo(position.Cell());
    Require(after[0] == before[0] && after[1] >= before[1],
            "clear must remove intent queued in the session before the next step");
}
void RunningPlayerAndGlobalPause() {
    Fixture f;
    Require(!f.Session.IsPaused(), "normal game must start running");
    const auto start = f.Session.GetPlayer()->GetWorldPosition();
    f.KeyEvent(Key::KEY_D);
    f.KeyEvent(Key::KEY_SPACE);
    f.Session.Tick(1.f / 60, f.Input.ConsumePlayerInput());
    const auto moved = f.Session.GetPlayer()->GetWorldPosition().RelativeTo(start.Cell());
    const auto initial = start.RelativeTo(start.Cell());
    Require(moved[0] > initial[0] && moved[1] < initial[1], "new game must accept walking and jumping without a resume action");

    Require(f.Session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{true}).Succeeded(), "game pause failed");
    const auto stamp = f.Session.CurrentCommit();
    const auto frozen = f.Session.GetPlayer()->CaptureState();
    const auto camera = f.Camera.GetCamera().GetWorldPosition();
    f.KeyEvent(Key::KEY_SPACE);
    f.Session.Tick(.2f, f.Input.ConsumePlayerInput());
    f.Input.UpdateCamera(.2f);
    Require(f.Session.CurrentCommit() == stamp && f.Session.GetPlayer()->GetWorldPosition().Local == frozen.Position.Local
                && f.Camera.GetCamera().GetWorldPosition().Local == camera.Local,
            "global pause must freeze the player, game clock and following camera");
    Require(f.Session.Execute(pixel::NativeCaller::Input, pixel::api::MineRegion{{520, 512}}).Rejection == pixel::api::Reason::GamePaused,
            "global pause accepted a world-changing input action");
    f.KeyEvent(Key::KEY_F3);
    const auto debug = f.Camera.GetCamera().GetPosition();
    f.KeyEvent(Key::KEY_D);
    f.Input.UpdateCamera(.1f);
    Require(f.Camera.GetCamera().GetPosition()[0] > debug[0] && f.Session.CurrentCommit() == stamp,
            "global pause must leave debug camera responsive without changing the world");
    f.KeyEvent(Key::KEY_F3);
    Require(f.Session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false}).Succeeded(), "game resume failed");
    Require(!f.Input.ConsumePlayerInput().Jump, "resume replayed a jump pressed while paused");
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
    Require(f.Camera.GetView().CellSizePixels[0] == std::min(2 * pixel::DefaultRenderPixelsPerLogicalPixel, pixel::MaximumRenderPixelsPerLogicalPixel) && f.Camera.GetCamera().GetPosition() == centre,
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
    Require(!f.Session.IsPaused() && f.Session.GetSimulationTime() == 0 && pixel::testing::SessionAccess::World(f.Session).GetWindEndRow() == 424 &&
                pixel::testing::SessionAccess::World(f.Session).GetCell(20, 512).Type == pixel::CellTypes::Dirt,
            "R must reset simulation and terrain");
    Require(f.Input.GetControlMode() == pixel::ControlMode::DebugCamera && f.Presentation.DisplayMode == GridDisplayMode::ForceField,
            "R must preserve control and display modes");
    Require(f.Camera.GetFramebufferSize() == f.Framebuffer && f.Camera.GetView().CellSizePixels == pixel::DefaultLogicalPixelSize &&
                f.Camera.GetCamera().GetFollowRate() == 4 && f.Presentation.Post.Values[1] == 1,
            "R must refresh viewport and reset zoom/tint while retaining follow rate");
    const auto resetCentre = f.Camera.GetCamera().GetPosition();
    f.Input.UpdateCamera(.1f);
    Require(f.Camera.GetCamera().GetPosition() == resetCentre, "R must clear held camera movement");
}
void PendingResetKeepsView() {
    Fixture f;
    f.KeyEvent(Key::KEY_T);
    f.KeyEvent(Key::KEY_EQUAL);
    const auto view = f.Camera.GetView();
    const auto stamp = f.Session.CurrentCommit();
    pixel::testing::SessionAccess::RestoreClock(f.Session, 0, 1.0 / 60, true);
    f.KeyEvent(Key::KEY_R);
    Require(f.Session.LastPersistenceResult().Status == pixel::api::ControlStatus::Deferred
                && f.Session.CurrentCommit() == stamp && f.Session.GetTimeRemainder() == 1.0 / 60
                && f.Camera.GetView().CellSizePixels == view.CellSizePixels
                && f.Camera.GetView().OriginPixels == view.OriginPixels && f.Presentation.Post.Values[1] == .35f,
            "deferred R must preserve the session, camera and presentation");
}
void MouseEditing() {
    Fixture f;
    Require(f.Session.Execute(pixel::NativeCaller::Driver, pixel::api::MineRegion{{520, 512}, {}}).Applied(),
            "mouse fixture needs one soil item");
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
    f.Session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{true});
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
                std::abs(target->Local[0] - (.125 + .25 / pixel::DefaultRenderPixelsPerLogicalPixel)) < 1e-12 && std::abs(target->Local[1] - (.75 + .5 / pixel::DefaultRenderPixelsPerLogicalPixel)) < 1e-12,
            "continuous old-size event lost distant fractional aim");
    Require(!f.Camera.PickWorldPosition({{800, 300}, {800, 600}, {800, 600}}), "outside continuous aim accepted");
}

void DeviceInput() {
    Fixture f;
    auto &inventory = pixel::testing::SessionAccess::Inventory(f.Session);
    const std::array resources{pixel::items::ItemAmount{pixel::items::ItemId::Stone, 4},
                               pixel::items::ItemAmount{pixel::items::ItemId::CopperOre, 2},
                               pixel::items::ItemAmount{pixel::items::ItemId::Soil, 1}};
    Require(inventory.Add(inventory.Reference(f.Session.PlayerInventory()), resources) == pixel::items::Result::Applied,
            "device input fixture could not add building resources");
    f.Camera.GetCamera().SetPosition({100, 100});
    const MousePointerState target{{400, 300}, {800, 600}, {800, 600}};
    const double scale = pixel::DefaultRenderPixelsPerLogicalPixel;
    const MousePointerState body{{400 + scale, 300 + scale}, {800, 600}, {800, 600}};
    f.KeyEvent(Key::KEY_B);
    f.KeyEvent(Key::KEY_B, ButtonAction::ACTION_REPEAT);
    f.KeyEvent(Key::KEY_B, ButtonAction::ACTION_RELEASE);
    Require(f.Input.IsDevicePlacementEnabled(), "B must toggle placement once per press");
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_LEFT, ButtonAction::ACTION_PRESS, 0, target);
    Require(f.Session.GetDevices().Count() == 1 && f.Session.LastAction(pixel::NativeCaller::Input)->Applied(),
            "build mode left click did not place a device");
    const auto device = f.Session.GetDevices().Snapshot().front();
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_PRESS, 0, body);
    Require(f.Session.GetDevices().Snapshot().front().Enabled != device.Enabled &&
                f.Session.GetFormulaRuntime().Projectiles().empty(), "device body right click did not toggle exclusively");
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_PRESS, int(Mod::MOD_SHIFT), body);
    Require(f.Session.GetDevices().Count() == 0, "Shift+right click did not recycle device");
    f.KeyEvent(Key::KEY_B);
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_LEFT, ButtonAction::ACTION_PRESS, 0, target);
    Require(!f.Input.IsDevicePlacementEnabled() && f.Session.GetWorld().GetCell(100, 100).Type == pixel::CellTypeId::Dirt,
            "leaving build mode did not restore material placement");
}
void InventoryInput() {
    Fixture f;
    f.KeyEvent(Key::KEY_TAB);
    f.KeyEvent(Key::KEY_TAB, ButtonAction::ACTION_REPEAT);
    f.KeyEvent(Key::KEY_TAB, ButtonAction::ACTION_RELEASE);
    Require(f.UI.IsExpanded(), "Tab must open inventory exactly once per press");
    f.KeyEvent(Key::KEY_7);
    Require(f.UI.Read().Selected == 6 && f.Input.GetElementBrush() == pixel::ElementBrush::Disabled,
            "number key did not select a quick slot exclusively");
    // Deliberately use an old 2x DPI event while the camera has a different size.
    f.Camera.SetFramebufferSize({800, 600});
    const auto placement = pixel::ui::InventoryLayout::Placement({2560, 1440});
    const auto slot = pixel::ui::InventoryLayout::QuickSlot(2);
    const MousePointerState pointer{{(placement.X + (slot.X + 24) * 2) / 2,
                                     (placement.Y + (slot.Y + 24) * 2) / 2}, {1280, 720}, {2560, 1440}};
    const auto stamp = f.Session.CurrentCommit();
    const auto projectiles = f.Session.GetFormulaRuntime().Projectiles().size();
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_LEFT, ButtonAction::ACTION_PRESS, 0, pointer);
    f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_PRESS, 0, pointer);
    Require(f.UI.Read().Selected == 2 && f.Session.CurrentCommit() == stamp
                && f.Session.GetFormulaRuntime().Projectiles().size() == projectiles,
            "scaled panel click reached world actions or lost selection");
    f.KeyEvent(Key::KEY_ESCAPE);
    Require(!f.UI.IsExpanded() && f.QuitRequests == 0, "Escape should close inventory before quitting");
    f.KeyEvent(Key::KEY_ESCAPE);
    Require(f.QuitRequests == 1, "closed inventory should retain Escape quit behavior");
}

void ElementBrushInput() {
    Fixture f;
    f.Camera.GetCamera().SetPosition({100, 100});
    const MousePointerState pointer{{400, 300}, {800, 600}, {800, 600}};
    for (const auto [key, type] : {std::pair{Key::KEY_2, pixel::CellTypeId::Water},
                                  std::pair{Key::KEY_3, pixel::CellTypeId::Steam},
                                  std::pair{Key::KEY_4, pixel::CellTypeId::Fire}}) {
        pixel::testing::SessionAccess::World(f.Session).FillRegion({90, 90, 20, 20}, {});
        f.KeyEvent(key, ButtonAction::ACTION_PRESS, Mod::MOD_SHIFT);
        f.Input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_LEFT, ButtonAction::ACTION_PRESS, 0, pointer);
        Require(f.Session.GetWorld().GetCell(100, 100).Type == type
                    && f.Session.LastAction(pixel::NativeCaller::Debug)->Applied(),
                "element shortcut did not paint the clicked world position");
    }
    f.KeyEvent(Key::KEY_B);
    Require(f.Input.GetElementBrush() == pixel::ElementBrush::Disabled && f.Input.IsDevicePlacementEnabled(),
            "device and element tools overlap");
    f.KeyEvent(Key::KEY_3, ButtonAction::ACTION_PRESS, Mod::MOD_SHIFT);
    Require(!f.Input.IsDevicePlacementEnabled(), "element tool retained device placement");
    f.KeyEvent(Key::KEY_1, ButtonAction::ACTION_PRESS, Mod::MOD_SHIFT);
    Require(f.Input.GetElementBrush() == pixel::ElementBrush::Disabled, "normal tool shortcut did not leave the brush");
}

} // namespace
int main() {
    try {
        HeldInputAndModes();
        RunningPlayerAndGlobalPause();
        CommandsAndReset();
        PendingResetKeepsView();
        MouseEditing();
        FormulaInputAndContinuousAim();
        DeviceInput();
        InventoryInput();
        ElementBrushInput();
        std::cout << "Pixel input behavior passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
