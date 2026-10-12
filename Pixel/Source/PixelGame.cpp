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
    World.SpawnPlayer(Grid2D::Width / 2 - pixel::Character::Width / 2, Grid2D::Height - TerrainHeight - pixel::Character::Height);
    ClearInputState();
    WindKind = pixel::WindStrategyKind::Coarse;
    PauseSimulation = StepSimulation = false;
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
        if (World.GetPlayer())
            BuildCharacterFrame(*World.GetPlayer(), frame);
    };
}
GridTextureCatalog PixelGame::MakeTextures() {
    // Transparent air, brown dirt, warm head and turquoise body (RGBA8/sRGB).
    GridTextureCatalog textures{1, 1, 4, {}};
    for (const auto color : {std::array{0, 0, 0, 0}, std::array{151, 104, 65, 255},
                             std::array{255, 212, 140, 255}, std::array{40, 210, 180, 255}})
        for (int channel : color)
            textures.Pixels.push_back(std::byte(channel));
    return textures;
}
void PixelGame::Initialize() {
    GAxiom->Engine->GetPlatform()->SetCursorInputMode(true);
    ResetView();
    if (auto *ui = GAxiom->Engine->GetImgui())
        ui->SetFrameContent([this] { DrawDebugUI(); });
    spdlog::info("Pixel: left click place dirt; right click remove dirt; A/D move; Space jump; F3 debug camera (WASD pan); +/- zoom; F1 toggle force-field view; F2 toggle Debug UI; T toggle tint; PageDown next region; R reset world; Escape quit");
}
void PixelGame::SetWindStartHeight(int height) {
    const auto worldHeight = World.ReadState().Height;
    const auto endRow = worldHeight - uint32_t(std::clamp(height, 0, int(worldHeight)));
    if (World.GetWind().IsEnabled())
        DebugWindSettings = World.GetWind().GetSettings();
    if (endRow == 0)
        World.DisableWind();
    else if (endRow != World.GetWindEndRow() || WindKind != World.GetWindStrategyKind()) {
        pixel::WindStrategySettings settings;
        settings.Kind = WindKind;
        settings.Coarse = DebugWindSettings;
        World.ConfigureWindStrategy(endRow, settings);
    }
}
void PixelGame::DrawDebugUI() {
    if (!ShowDebug)
        return;
    ImGui::SetNextWindowPos({16, 16}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Pixel Debug", &ShowDebug, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
            const int worldHeight = int(World.ReadState().Height);
            int height = worldHeight - int(World.GetWindEndRow());
            ImGui::TextUnformatted("Wind blows above the selected height.");
            ImGui::TextUnformatted("Height is measured from the world bottom.");
            ImGui::SetNextItemWidth(190);
            if (ImGui::SliderInt("Wind start height", &height, 0, worldHeight, "%d cells", ImGuiSliderFlags_AlwaysClamp))
                SetWindStartHeight(height);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drag to adjust, or %s+click to enter a height.", ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd" : "Ctrl");
            ImGui::Text("Wind coverage: %u / %d rows", World.GetWindEndRow(), worldHeight);
            ImGui::Text("0 = whole world; %d = wind off", worldHeight);
            ImGui::TextDisabled("Changing height restarts the wind simulation.");
            if (ImGui::Button("Default height (600)"))
                SetWindStartHeight(DefaultWindStartHeight);
            int selected = int(WindKind);
            if (ImGui::Combo("Wind strategy", &selected, "Coarse NS\0Pixel reference (1:1)\0Procedural + obstacles\0Coarse NS + local model\0Adaptive NS (CPU prototype)\0LBM D2Q9 (CPU prototype)\0")) {
                WindKind = pixel::WindStrategyKind(selected);
                PauseSimulation = WindKind == pixel::WindStrategyKind::Reference || WindKind == pixel::WindStrategyKind::Adaptive || WindKind == pixel::WindStrategyKind::Lbm;
                StepSimulation = false;
                SetWindStartHeight(height);
            }
            ImGui::Checkbox("Pause simulation", &PauseSimulation);
            if (PauseSimulation && ImGui::Button("Step 1/60 second"))
                StepSimulation = true;
            if (World.UsesPixelWind()) {
                const auto &report = World.GetPixelWind().GetReport();
                ImGui::Text("Divergence RMS: %.3g (%s)", report.RmsDivergence, report.Converged ? "converged" : "NOT converged");
                ImGui::TextDisabled("CPU reference: use single steps for large worlds.");
            }
            if (const auto *adaptive = World.GetAdaptiveWind()) {
                const auto &report = adaptive->GetReport();
                ImGui::Text("Leaves: %zu; face divergence RMS: %.3g (%s)", adaptive->GetLeafCount(), report.RmsDivergence, report.Converged ? "converged" : "NOT converged");
                ImGui::TextDisabled("First-order CPU prototype; use single steps.");
            }
            if (const auto *lbm = World.GetLbmWind()) {
                const auto &report = lbm->GetReport();
                ImGui::Text("Density: %.3g..%.3g; stabilizations: %llu", report.MinDensity, report.MaxDensity, static_cast<unsigned long long>(report.Stabilizations));
                ImGui::Text("Effective viscosity: %.3g", report.EffectiveViscosity);
                ImGui::TextDisabled("Weakly compressible CPU prototype; use single steps.");
            }
            bool fieldView = DisplayMode == GridDisplayMode::ForceField;
            if (ImGui::Checkbox("Field direction view (F1)", &fieldView))
                DisplayMode = fieldView ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
        }
        bool debugCamera = Mode == ControlMode::DebugCamera;
        if (ImGui::Checkbox("Free debug camera (F3)", &debugCamera))
            SetControlMode(debugCamera ? ControlMode::DebugCamera : ControlMode::Standard);
        ImGui::TextUnformatted(debugCamera ? "DEBUG: WASD camera; +/- zoom; PageDown region" : "STANDARD: A/D move; Space jump; camera follows");
        ImGui::TextDisabled("F2: show/hide debug panel; T: tint");
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
    if (Mode == ControlMode::Standard)
        FollowPlayer();
}
void PixelGame::ClearInputState() {
    Move.fill(false);
    JumpRequested = false;
    World.SetPlayerInput(0, false);
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
    World.SetPlayerInput(Mode == ControlMode::Standard ? int(Move[3]) - int(Move[1]) : 0,
                         Mode == ControlMode::Standard && JumpRequested);
    if (!PauseSimulation || StepSimulation)
        World.Tick(StepSimulation ? 1.f / 60 : duration);
    JumpRequested = false;
    StepSimulation = false;
    if (Mode == ControlMode::Standard) {
        FollowPlayer();
        return;
    }
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
    // Repeats must not reactivate a held key after mode/UI input was cleared.
    for (size_t i = 0; i < keys.size(); ++i)
        if (key == keys[i] && (action == ButtonAction::ACTION_PRESS || action == ButtonAction::ACTION_RELEASE))
            Move[i] = action == ButtonAction::ACTION_PRESS;
    if (action != ButtonAction::ACTION_PRESS)
        return;
    if (key == Key::KEY_ESCAPE)
        GAxiom->Engine->GetPlatform()->CloseWindow();
    if (key == Key::KEY_SPACE && Mode == ControlMode::Standard)
        JumpRequested = true;
    if (key == Key::KEY_T || (key == Key::KEY_SPACE && Mode == ControlMode::DebugCamera))
        Post.Values[1] = Post.Values[1] == 1 ? 0.35f : 1;
    if (key == Key::KEY_F1)
        DisplayMode = DisplayMode == GridDisplayMode::Materials ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
    if (key == Key::KEY_F3)
        SetControlMode(Mode == ControlMode::Standard ? ControlMode::DebugCamera : ControlMode::Standard);
    if (key == Key::KEY_F2)
        ShowDebug = !ShowDebug;
    if (key == Key::KEY_R) {
        ResetWorld();
        ResetView();
    }
    if (key == Key::KEY_PAGE_DOWN && Mode == ControlMode::DebugCamera)
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

void PixelGame::FollowPlayer() {
    if (!World.GetPlayer())
        return;
    int width = 0, height = 0;
    GAxiom->Engine->GetPlatform()->GetFramebufferSize(width, height);
    const auto &player = *World.GetPlayer();
    View.OriginPixels = {int((player.X + pixel::Character::Width / 2) * View.CellSizePixels[0]) - width / 2,
                         int((player.Y + pixel::Character::Height / 2) * View.CellSizePixels[1]) - height / 2};
}
void PixelGame::SetControlMode(ControlMode mode) {
    if (mode == Mode)
        return;
    Mode = mode;
    ClearInputState();
    MovementRemainder = 0;
    if (Mode == ControlMode::Standard)
        FollowPlayer();
}
