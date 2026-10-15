#include "PixelDebugUI.h"
#include "Camera/GameCamera.h"
#include "Gameplay/GameSession.h"
#include "Performance/GameProfiler.h"
#include "PixelRender/GamePresentation.h"
#include "imgui.h"

namespace pixel {
namespace {
void DrawEnvironment(GameSession &session, GamePresentation &presentation) {
    auto &World = session.GetWorld();
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        const int worldHeight = int(World.ReadState().Height);
        int height = worldHeight - int(World.GetWindEndRow());
        ImGui::TextUnformatted("Wind blows above the selected height.");
        ImGui::TextUnformatted("Height is measured from the world bottom.");
        ImGui::SetNextItemWidth(190);
        if (ImGui::SliderInt("Wind start height", &height, 0, worldHeight, "%d cells", ImGuiSliderFlags_AlwaysClamp))
            session.SetWindStartHeight(height);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drag to adjust, or %s+click to enter a height.", ImGui::GetIO().ConfigMacOSXBehaviors ? "Cmd" : "Ctrl");
        ImGui::Text("Wind coverage: %u / %d rows", World.GetWindEndRow(), worldHeight);
        ImGui::Text("0 = whole world; %d = wind off", worldHeight);
        ImGui::TextDisabled("Changing height restarts the wind simulation.");
        if (ImGui::Button("Default height (600)"))
            session.SetWindStartHeight(GameSession::DefaultWindStartHeight);
        int selected = int(session.GetSelectedWindStrategy());
        if (ImGui::Combo("Wind strategy", &selected, "Coarse NS\0Pixel reference (1:1)\0Procedural + obstacles\0Coarse NS + local model\0Adaptive NS (CPU prototype)\0LBM D2Q9 (CPU prototype)\0Empty (no wind)\0")) {
            session.SelectWindStrategy(WindStrategyKind(selected));
        }
        if (session.GetSelectedWindStrategy() == WindStrategyKind::Empty)
            ImGui::TextDisabled("No wind simulation; height is kept for comparison.");
        bool paused = session.IsPaused();
        if (ImGui::Checkbox("Pause simulation", &paused))
            session.SetPaused(paused);
        if (paused && ImGui::Button("Step 1/60 second"))
            session.RequestStep();
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
        bool fieldView = presentation.DisplayMode == GridDisplayMode::ForceField;
        if (ImGui::Checkbox("Field direction view (F1)", &fieldView))
            presentation.DisplayMode = fieldView ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
    }
}
void DrawWindAppearance(GamePresentation &presentation) {
    if (ImGui::CollapsingHeader("Wind appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        int mode = int(presentation.WindVisual.Mode);
        if (ImGui::Combo("Visualization", &mode, "Off\0Short trails\0Air refraction\0"))
            presentation.WindVisual.Mode = WindVisualMode(mode);
        ImGui::SliderFloat("Visibility", &presentation.WindVisual.Opacity, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Refraction", &presentation.WindVisual.RefractionPixels, 0.f, 12.f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Flow motion", &presentation.WindVisual.MotionScale, 0.f, 2.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("Appearance only; wind physics is unchanged.");
        ImGui::TextDisabled("Select a wind strategy to compare. F3: explore.");
    }
}
} // namespace
std::optional<ControlMode> PixelDebugUI::Draw(GameSession &session, GameCamera &camera,
                                               GamePresentation &presentation, ControlMode mode,
                                               performance::GameProfiler &performance) {
    PIXEL_PROFILE_SCOPE(DebugUI);
    if (!Visible)
        return std::nullopt;
    std::optional<ControlMode> requestedMode;
    ImGui::SetNextWindowPos({16, 16}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({360, 0}, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Pixel Debug", &Visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        DrawEnvironment(session, presentation);
        DrawWindAppearance(presentation);
        bool debugCamera = mode == ControlMode::DebugCamera;
        if (ImGui::Checkbox("Free debug camera (F3)", &debugCamera))
            requestedMode = debugCamera ? ControlMode::DebugCamera : ControlMode::Standard;
        ImGui::TextUnformatted(debugCamera ? "DEBUG: WASD camera; +/- zoom; PageDown region" : "STANDARD: A/D move; Space jump; camera follows");
        float followRate = float(camera.GetCamera().GetFollowRate());
        if (ImGui::SliderFloat("Camera follow rate", &followRate, 0.5f, 20.f, "%.1f /s", ImGuiSliderFlags_AlwaysClamp))
            camera.GetCamera().SetFollowRate(followRate);
        ImGui::TextDisabled("F2: show/hide debug panel; T: tint");
        performance.DrawUI();
    }
    ImGui::End();
    return requestedMode;
}
} // namespace pixel
