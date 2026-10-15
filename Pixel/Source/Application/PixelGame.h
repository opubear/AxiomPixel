#pragma once
#include "Camera/GameCamera.h"
#include "Debug/PixelDebugUI.h"
#include "GamePlay/GameInterface.h"
#include "Gameplay/GameSession.h"
#include "Input/GameInput.h"
#include "Performance/GameProfiler.h"
#include "PixelRender/GamePresentation.h"

// Engine lifecycle adapter and composition root. Each subsystem owns its state.
class PixelGame final : public IGameInterface {
  public:
    using ControlMode = pixel::ControlMode;
    PixelGame();
    // Input callbacks capture this composition root; its address remains stable.
    PixelGame(const PixelGame &) = delete;
    PixelGame &operator=(const PixelGame &) = delete;
    void ConfigurePerformanceCapture();
    void SetControlMode(ControlMode mode) { Input.SetControlMode(mode); }
    ControlMode GetControlMode() const { return Input.GetControlMode(); }
    std::function<void(WorldGridFrame &)> CreateFramePublisher() {
        return Presentation.CreateFramePublisher(Session, Camera);
    }
    pixel::World &GetWorld() { return Session.GetWorld(); }
    const pixel::World &GetWorld() const { return Session.GetWorld(); }
    pixel::GamePresentation &GetPresentation() { return Presentation; }
    const pixel::GamePresentation &GetPresentation() const { return Presentation; }
    GridView2D GetView() const { return Camera.GetView(); }
    void SetView(const GridView2D &view) { Camera.SetView(view); }
    const pixel::Camera2D &GetCamera() const { return Camera.GetCamera(); }
    void Initialize() override;
    void GameBegin() override {}
    void FrameBegin() override;
    void Tick(float duration) override;
    void ClearInputState() override { Input.ClearInputState(); }
    void FrameEnd() override;
    void GameEnd() override {}
    void Destroy() override {}
    void OnKeyEvent(Key key, int scancode, ButtonAction action, Mod mods) override {
        Input.OnKeyEvent(key, scancode, action, mods);
    }
    void OnMouseMove(float x, float y) override { Input.OnMouseMove(x, y); }
    void OnMouseMoveDelta(float x, float y) override { Input.OnMouseMoveDelta(x, y); }
    void OnMouseButtonEvent(MouseButton button, ButtonAction action, int mods, const MousePointerState &pointer) override {
        Input.OnMouseButtonEvent(button, action, mods, pointer);
    }

  private:
    pixel::GameSession Session;
    pixel::GameCamera Camera;
    pixel::GamePresentation Presentation;
    pixel::PixelDebugUI DebugUI;
    pixel::performance::GameProfiler Performance;
    pixel::GameInput Input;
};
