#pragma once
#include "GamePlay/GameInterface.h"
#include "PixelRender/PixelWorldPresentation.h"
#include "Render/RenderSetup.h"
#include "World/World.h"
#include <functional>

class PixelGame final : public IGameInterface {
  public:
    PixelGame();
    static GridTextureCatalog MakeTextures();
    RenderSetup CreateRenderSetup();
    // Transfer this callback to Render; its snapshot belongs to the consumer.
    std::function<void(WorldGridFrame &)> CreateFramePublisher();
    pixel::World &GetWorld() { return World; }
    const pixel::World &GetWorld() const { return World; }
    void Initialize() override;
    void GameBegin() override {}
    void FrameBegin() override {}
    void Tick(float duration) override;
    void ClearInputState() override;
    void FrameEnd() override {}
    void GameEnd() override {}
    void Destroy() override {}
    void OnKeyEvent(Key key, int scancode, ButtonAction action, Mod mods) override;
    void OnMouseMove(float, float) override {}
    void OnMouseMoveDelta(float, float) override {}
    void OnMouseButtonEvent(MouseButton button, ButtonAction action, int mods, const MousePointerState &pointer) override;
    CellAppearanceTable Appearances{{pixel::CellTypes::Dirt.Value, 1}};
    GridView2D View;
    PostProcessParameters Post;
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;

  private:
    pixel::World World;
    std::array<bool, 4> Move{};
    float MovementRemainder = 0;
    bool ShowDebug = true;
    pixel::WindSettings DebugWindSettings;
    void DrawDebugUI();
    void SetWindStartHeight(int height);
    void ResetWorld();
    void ResetView();
};
