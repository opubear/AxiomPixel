#include "PixelGame.h"
#include "PixelRuntime.h"
#include "Axiom.h"
#include "DebugUI/ImguiManager.h"
#include "Engine.h"
#include "HAL/Platform/PlatformInterface.h"
#include "InputManager.h"
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

void PixelGame::ConfigureEngine(GameEngine &engine) {
    engine.SetRender(CreatePixelRenderSetup(*this));
    engine.GetInput()->RegisterFrameCallback([this](const FrameInput::Event &event) {
        switch (event.Type) {
        case FrameInput::Kind::Clear:
            ClearInputState();
            break;
        case FrameInput::Kind::Key:
            OnKeyEvent(static_cast<Key>(event.Code), event.Scancode,
                       event.Down ? (event.Repeat ? ButtonAction::ACTION_REPEAT : ButtonAction::ACTION_PRESS)
                                  : ButtonAction::ACTION_RELEASE,
                       static_cast<Mod>(event.Mods));
            break;
        case FrameInput::Kind::MouseButton:
            OnMouseButtonEvent(static_cast<MouseButton>(event.Code),
                               event.Down ? ButtonAction::ACTION_PRESS : ButtonAction::ACTION_RELEASE,
                               event.Mods, event.Pointer);
            break;
        case FrameInput::Kind::MouseMove:
            OnMouseMove(static_cast<float>(event.X), static_cast<float>(event.Y));
            OnMouseMoveDelta(static_cast<float>(event.DeltaX), static_cast<float>(event.DeltaY));
            break;
        }
    });
}

void PixelGame::OnMouseButtonEvent(MouseButton button, ButtonAction action, int mods) {
    // Direct interface calls have no queued event. Deferred input uses its snapshot above.
    auto *platform = GAxiom->Engine->GetPlatform();
    MousePointerState pointer;
    glfwGetCursorPos(platform->GetWindows(), &pointer.Position[0], &pointer.Position[1]);
    glfwGetWindowSize(platform->GetWindows(), &pointer.WindowSize[0], &pointer.WindowSize[1]);
    platform->GetFramebufferSize(pointer.FramebufferSize[0], pointer.FramebufferSize[1]);
    OnMouseButtonEvent(button, action, mods, pointer);
}

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
    if (auto *ui = GAxiom->GetDebugUI())
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
