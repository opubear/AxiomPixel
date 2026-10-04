#include "PixelGame.h"
#include "Axiom.h"
#include "Engine.h"
#include "HAL/Platform/PlatformInterface.h"
#include "PixelRender/PixelRender.h"
#include "SystemEnum.h"
#include "UI/Imgui/ImguiManager.h"
#include "imgui.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
// Gameplay heights rise from the bottom; grid row indices rise from the top.
constexpr uint32_t TerrainHeight = 512;
constexpr uint32_t DefaultWindStartHeight = 600;
static_assert(TerrainHeight < DefaultWindStartHeight && DefaultWindStartHeight <= Grid2D::Height);
} // namespace

PixelGame::PixelGame() : World(Grid2D::Width, Grid2D::Height) {
    ResetWorld();
    // Linear RGB: the presentation target encodes this as sky blue.
    View.Background = {0.25f, 0.55f, 0.85f, 1};
}
void PixelGame::ResetWorld() {
    World.GenerateTerrain(Grid2D::Height - TerrainHeight);
    DebugWindSettings = {};
    World.ConfigureWind(Grid2D::Height - DefaultWindStartHeight, DebugWindSettings);
}
RenderSetup PixelGame::CreateRenderSetup() {
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.EnableUI = true;
    setup.CreatePolicy = [this](DynamicRHI &rhi) {
        auto render = std::make_unique<PixelRender>(rhi, MakeTextures(), CreateFramePublisher());
        render->SetOverlay([](RHICommandList &commands) {
            GAxiom->Engine->GetImgui()->RecordDrawCommand(commands);
        });
        return render;
    };
    return setup;
}
std::function<void(WorldGridFrame &)> PixelGame::CreateFramePublisher() {
    const auto state = World.ReadState();
    auto snapshot = std::make_shared<pixel::WorldState>(state.Width, state.Height);
    return [this, snapshot = std::move(snapshot)](WorldGridFrame &frame) {
        World.PublishState(*snapshot);
        frame.View = View;
        frame.Post = Post;
        if (DisplayMode == GridDisplayMode::ForceField)
            BuildForceFieldFrame(World, frame);
        else
            BuildWorldGridFrame(snapshot->Read(), Appearances, frame);
    };
}
GridTextureCatalog PixelGame::MakeTextures() {
    // Empty layer followed by a solid brown dirt layer, both RGBA8/sRGB.
    return {1, 1, 2, {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{151}, std::byte{104}, std::byte{65}, std::byte{255}}};
}
void PixelGame::Initialize() {
    GAxiom->Engine->GetPlatform()->SetCursorInputMode(true);
    ResetView();
    if (auto *ui = GAxiom->Engine->GetImgui())
        ui->SetFrameContent([this] { DrawDebugUI(); });
    spdlog::info("Pixel: left click place dirt; right click remove dirt; WASD pan; +/- zoom; F1 toggle force-field view; F2 toggle Debug UI; Space toggle tint; PageDown next region; R reset world; Escape quit");
}
void PixelGame::SetWindStartHeight(int height) {
    const auto worldHeight = World.ReadState().Height;
    const auto endRow = worldHeight - uint32_t(std::clamp(height, 0, int(worldHeight)));
    if (World.GetWind().IsEnabled())
        DebugWindSettings = World.GetWind().GetSettings();
    if (endRow == 0)
        World.DisableWind();
    else if (endRow != World.GetWind().GetEndRow())
        World.ConfigureWind(endRow, DebugWindSettings);
}
void PixelGame::DrawDebugUI() {
    if (!ShowDebug)
        return;
    ImGui::SetNextWindowPos({16, 16}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Pixel Debug", &ShowDebug, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
            const int worldHeight = int(World.ReadState().Height);
            int height = worldHeight - int(World.GetWind().GetEndRow());
            ImGui::TextUnformatted("Wind blows above the selected height.");
            ImGui::TextUnformatted("Height is measured from the world bottom.");
            ImGui::SetNextItemWidth(190);
            if (ImGui::SliderInt("Wind start height", &height, 0, worldHeight, "%d cells", ImGuiSliderFlags_AlwaysClamp))
                SetWindStartHeight(height);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drag to adjust, or %s+click to enter a height.", ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd" : "Ctrl");
            ImGui::Text("Wind coverage: %u / %d rows", World.GetWind().GetEndRow(), worldHeight);
            ImGui::Text("0 = whole world; %d = wind off", worldHeight);
            ImGui::TextDisabled("Changing height restarts the wind simulation.");
            if (ImGui::Button("Default height (600)"))
                SetWindStartHeight(DefaultWindStartHeight);
            bool fieldView = DisplayMode == GridDisplayMode::ForceField;
            if (ImGui::Checkbox("Field direction view (F1)", &fieldView))
                DisplayMode = fieldView ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
        }
        ImGui::TextDisabled("F2: show/hide debug panel");
    }
    ImGui::End();
}
void PixelGame::ResetView() {
    int width = 0, height = 0;
    GAxiom->Engine->GetPlatform()->GetFramebufferSize(width, height);
    View.CellSizePixels = {16, 16};
    View.OriginPixels = {int(Grid2D::Width / 2 * 16) - width / 2, int((Grid2D::Height - TerrainHeight) * 16) - height / 2};
    Post = {};
    MovementRemainder = 0;
}
void PixelGame::ClearInputState() {
    Move.fill(false);
}
void PixelGame::OnMouseButtonEvent(MouseButton button, ButtonAction action, int, const MousePointerState &pointer) {
    if (action != ButtonAction::ACTION_PRESS || (button != MouseButton::MOUSE_BUTTON_LEFT && button != MouseButton::MOUSE_BUTTON_RIGHT))
        return;
    const auto cell = PickGridCell(View, pointer.Position, pointer.WindowSize, pointer.FramebufferSize);
    if (!cell)
        return;
    if (button == MouseButton::MOUSE_BUTTON_LEFT)
        World.PlaceDirt((*cell)[0], (*cell)[1]);
    else
        World.RemoveDirt((*cell)[0], (*cell)[1]);
}
void PixelGame::Tick(float duration) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    World.Tick(duration);
    MovementRemainder += std::min(duration, 0.25f) * 240;
    const int delta = int(MovementRemainder);
    MovementRemainder -= float(delta);
    View.OriginPixels[0] += (int(Move[3]) - int(Move[1])) * delta;
    View.OriginPixels[1] += (int(Move[2]) - int(Move[0])) * delta;
    for (size_t axis = 0; axis < 2; ++axis) {
        const auto maximum = (axis == 0 ? Grid2D::Width : Grid2D::Height) * View.CellSizePixels[axis];
        View.OriginPixels[axis] = std::clamp(View.OriginPixels[axis], -512, int(maximum));
    }
}
void PixelGame::OnKeyEvent(Key key, int, ButtonAction action, Mod) {
    const std::array keys{Key::KEY_W, Key::KEY_A, Key::KEY_S, Key::KEY_D};
    for (size_t i = 0; i < keys.size(); ++i)
        if (key == keys[i])
            Move[i] = action != ButtonAction::ACTION_RELEASE;
    if (action != ButtonAction::ACTION_PRESS)
        return;
    if (key == Key::KEY_ESCAPE)
        GAxiom->Engine->GetPlatform()->CloseWindow();
    if (key == Key::KEY_SPACE)
        Post.Values[1] = Post.Values[1] == 1 ? 0.35f : 1;
    if (key == Key::KEY_F1)
        DisplayMode = DisplayMode == GridDisplayMode::Materials ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
    if (key == Key::KEY_F2)
        ShowDebug = !ShowDebug;
    if (key == Key::KEY_R) {
        ResetWorld();
        ResetView();
    }
    if (key == Key::KEY_PAGE_DOWN)
        View.OriginPixels[1] = ((std::max(0, View.OriginPixels[1]) / int(View.CellSizePixels[1]) / int(Grid2D::PageHeight) + 1) % int(Grid2D::PageCount)) * int(Grid2D::PageHeight * View.CellSizePixels[1]);
    if (key == Key::KEY_EQUAL || key == Key::KEY_MINUS) {
        const uint32_t old = View.CellSizePixels[0];
        const uint32_t next = key == Key::KEY_EQUAL ? std::min(64u, old * 2) : std::max(1u, old / 2);
        std::array<int, 2> extent{};
        GAxiom->Engine->GetPlatform()->GetFramebufferSize(extent[0], extent[1]);
        for (size_t axis = 0; axis < 2; ++axis) {
            View.OriginPixels[axis] = int((int64_t(View.OriginPixels[axis]) + extent[axis] / 2) * next / old) - extent[axis] / 2;
            View.CellSizePixels[axis] = next;
        }
    }
}
