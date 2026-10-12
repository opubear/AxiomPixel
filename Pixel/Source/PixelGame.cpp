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
#include <stdexcept>

namespace {
// Gameplay heights rise from the bottom; grid row indices rise from the top.
constexpr uint32_t TerrainHeight = 512;
constexpr uint32_t DefaultWindStartHeight = 600;
static_assert(TerrainHeight < DefaultWindStartHeight && DefaultWindStartHeight <= Grid2D::Height);
} // namespace

PixelGame::PixelGame() : World(Grid2D::Width, Grid2D::Height) {
    ResetWorld();
    // Linear RGB: the presentation target encodes this as sky blue.
    ViewSettings.Background = {0.25f, 0.55f, 0.85f, 1};
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
        frame.View = GetView();
        frame.Post = Post;
        if (DisplayMode == GridDisplayMode::ForceField)
            BuildForceFieldFrame(World, frame);
        else
            BuildWorldGridFrame(snapshot->Read(), Appearances, frame);
        if (World.GetPlayer())
            BuildCharacterFrame(*World.GetPlayer(), Camera, FramebufferSize, frame);
    };
}
GridTextureCatalog PixelGame::MakeTextures() {
    // Terrain textures only; the character is a separate continuous-colour draw.
    GridTextureCatalog textures{1, 1, 2, {}};
    for (const auto color : {std::array{0, 0, 0, 0}, std::array{151, 104, 65, 255}})
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
        float followRate = float(Camera.GetFollowRate());
        if (ImGui::SliderFloat("Camera follow rate", &followRate, 0.5f, 20.f, "%.1f /s", ImGuiSliderFlags_AlwaysClamp))
            Camera.SetFollowRate(followRate);
        ImGui::TextDisabled("F2: show/hide debug panel; T: tint");
    }
    ImGui::End();
}
void PixelGame::FrameBegin() {
    GAxiom->Engine->GetPlatform()->GetFramebufferSize(FramebufferSize[0], FramebufferSize[1]);
}
GridView2D PixelGame::GetView() const {
    return MakeCameraView(Camera, FramebufferSize, ViewSettings);
}
void PixelGame::SetView(const GridView2D &view) {
    for (auto size : view.CellSizePixels)
        if (size < 1 || size > 64)
            throw std::invalid_argument("cell size must be in [1, 64]");
    Camera.SetPosition({(double(view.OriginPixels[0]) + FramebufferSize[0] / 2.0) / view.CellSizePixels[0],
                        (double(view.OriginPixels[1]) + FramebufferSize[1] / 2.0) / view.CellSizePixels[1]});
    ViewSettings = view;
}
void PixelGame::ResetView() {
    FrameBegin();
    ViewSettings.CellSizePixels = {16, 16};
    Camera.SetPosition({Grid2D::Width / 2.0, double(Grid2D::Height - TerrainHeight)});
    if (World.GetPlayer()) {
        const auto &player = *World.GetPlayer();
        Camera.SetPosition({player.X + pixel::Character::Width / 2, player.Y + pixel::Character::Height / 2});
    }
    Post = {};
}
void PixelGame::ClearInputState() {
    Move.fill(false);
    JumpRequested = false;
    World.SetPlayerInput(0, false);
}
void PixelGame::OnMouseButtonEvent(MouseButton button, ButtonAction action, int, const MousePointerState &pointer) {
    if (action != ButtonAction::ACTION_PRESS || (button != MouseButton::MOUSE_BUTTON_LEFT && button != MouseButton::MOUSE_BUTTON_RIGHT))
        return;
    if (pointer.FramebufferSize[0] <= 0 || pointer.FramebufferSize[1] <= 0)
        return;
    // A queued click may predate a resize. Its projection and cursor scaling
    // must use the same framebuffer dimensions captured with the event.
    const auto eventView = MakeCameraView(Camera, pointer.FramebufferSize, ViewSettings);
    const auto cell = PickGridCell(eventView, pointer.Position, pointer.WindowSize, pointer.FramebufferSize);
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
    const float cameraDuration = std::min(duration, 0.25f);
    if (Mode == ControlMode::Standard) {
        FollowPlayer(cameraDuration);
    } else {
        // Keep the existing 240 framebuffer pixels/second debug pan speed.
        Camera.Translate({(int(Move[3]) - int(Move[1])) * cameraDuration * 240 / ViewSettings.CellSizePixels[0],
                          (int(Move[2]) - int(Move[0])) * cameraDuration * 240 / ViewSettings.CellSizePixels[1]});
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
    if (key == Key::KEY_PAGE_DOWN && Mode == ControlMode::DebugCamera) {
        auto view = GetView();
        view.OriginPixels[1] = ((std::max(0, view.OriginPixels[1]) / int(view.CellSizePixels[1]) / int(Grid2D::PageHeight) + 1)
                               % int(Grid2D::PageCount)) * int(Grid2D::PageHeight * view.CellSizePixels[1]);
        SetView(view);
    }
    if (key == Key::KEY_EQUAL || key == Key::KEY_MINUS) {
        const uint32_t old = ViewSettings.CellSizePixels[0];
        const uint32_t next = key == Key::KEY_EQUAL ? std::min(64u, old * 2) : std::max(1u, old / 2);
        ViewSettings.CellSizePixels = {next, next};
    }
}

void PixelGame::FollowPlayer(float duration) {
    if (!World.GetPlayer())
        return;
    const auto &player = *World.GetPlayer();
    Camera.Follow({player.X + pixel::Character::Width / 2, player.Y + pixel::Character::Height / 2}, duration);
}
void PixelGame::SetControlMode(ControlMode mode) {
    if (mode == Mode)
        return;
    Mode = mode;
    ClearInputState();
    // Keep camera position across mode changes; standard mode eases back in Tick.
}
