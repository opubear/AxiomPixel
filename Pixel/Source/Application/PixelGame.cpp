#include "PixelGame.h"
#include "Axiom.h"
#include "Engine.h"
#include "HAL/Platform/PlatformInterface.h"
#include "UI/Imgui/ImguiManager.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cmath>

PixelGame::PixelGame()
    : Input(Session, Camera, Presentation, {
          [] { GAxiom->Engine->GetPlatform()->CloseWindow(); },
          [this] { DebugUI.Toggle(); },
          [] {
              std::array<int, 2> size{};
              GAxiom->Engine->GetPlatform()->GetFramebufferSize(size[0], size[1]);
              return size;
          }}) {}
void PixelGame::ConfigurePerformanceCapture() {
    const auto scenario = Performance.ConfigureFromEnvironment();
    if (scenario == "paused" || scenario == "paused-no-wind-visual")
        Session.SetPaused(true);
    if (scenario == "no-wind-visual" || scenario == "paused-no-wind-visual")
        Presentation.WindVisual.Mode = WindVisualMode::Off;
    if (scenario == "field")
        Presentation.DisplayMode = GridDisplayMode::ForceField;
}
void PixelGame::Initialize() {
    GAxiom->Engine->GetPlatform()->SetCursorInputMode(true);
    Input.ResetView();
    if (auto *ui = GAxiom->Engine->GetImgui())
        ui->SetFrameContent([this] {
            if (const auto mode = DebugUI.Draw(Session, Camera, Presentation, GetControlMode(), Performance))
                SetControlMode(*mode);
        });
    spdlog::info("{}", pixel::GameInput::ControlsDescription());
}
void PixelGame::FrameBegin() {
    Performance.BeginFrame();
    std::array<int, 2> size{};
    GAxiom->Engine->GetPlatform()->GetFramebufferSize(size[0], size[1]);
    Camera.SetFramebufferSize(size);
    pixel::performance::FrameProfiler::Count(pixel::performance::Counter::FramebufferPixels,
        uint64_t(std::max(0, size[0])) * uint64_t(std::max(0, size[1])));
}
void PixelGame::FrameEnd() {
    if (Performance.IsCollecting())
        Performance.SetMemoryUsage(Session.GetWorld().GetMemoryUsage());

    if (Performance.EndFrame())
        GAxiom->Engine->GetPlatform()->CloseWindow();
}
void PixelGame::Tick(float duration) {
    PIXEL_PROFILE_SCOPE(GameTick);
    if (!std::isfinite(duration) || duration <= 0)
        return;
    Session.Tick(duration, Input.ConsumePlayerInput());
    Input.UpdateCamera(duration);
}
