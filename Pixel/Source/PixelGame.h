#pragma once
#include "GamePlay/GameInterface.h"
#include "PixelRender/PixelWorldPresentation.h"
#include "Render/RenderSetup.h"
#include "World/World.h"
#include <functional>

class PixelGame final : public IGameInterface {
  public:
    enum class ControlMode { Standard, DebugCamera };
    PixelGame();
    void SetControlMode(ControlMode mode);
    ControlMode GetControlMode() const { return Mode; }
    static GridTextureCatalog MakeTextures();
    RenderSetup CreateRenderSetup();
    // Transfer this callback to Render; its snapshot belongs to the consumer.
    std::function<void(WorldGridFrame &)> CreateFramePublisher();
    pixel::World &GetWorld() { return World; }
    const pixel::World &GetWorld() const { return World; }
    void Initialize() override;
    void GameBegin() override {}
    void FrameBegin() override;
    void Tick(float duration) override;
    void ClearInputState() override;
    void FrameEnd() override {}
    void GameEnd() override {}
    void Destroy() override {}
    void OnKeyEvent(Key key, int scancode, ButtonAction action, Mod mods) override;
    void OnMouseMove(float, float) override {}
    void OnMouseMoveDelta(float, float) override {}
    void OnMouseButtonEvent(MouseButton button, ButtonAction action, int mods, const MousePointerState &pointer) override;
    CellAppearanceTable Appearances{{static_cast<uint32_t>(pixel::CellTypeId::Dirt), 1}};
    GridView2D GetView() const;
    // Explicitly restore an editor/test view by converting its origin to camera position.
    void SetView(const GridView2D &view);
    const pixel::Camera2D &GetCamera() const { return Camera; }
    PostProcessParameters Post;
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;

  private:
    pixel::World World;
    std::array<bool, 4> Move{};
    pixel::Camera2D Camera;
    GridView2D ViewSettings;
    std::array<int, 2> FramebufferSize{};
    bool ShowDebug = true;
    ControlMode Mode = ControlMode::Standard;
    bool JumpRequested = false;
    void FollowPlayer(float duration);
    pixel::WindStrategyKind WindKind = pixel::WindStrategyKind::Coarse;
    bool PauseSimulation = false;
    bool StepSimulation = false;
    pixel::WindSettings DebugWindSettings;
    void DrawDebugUI();
    void SetWindStartHeight(int height);
    void ResetWorld();
    void ResetView();
};
