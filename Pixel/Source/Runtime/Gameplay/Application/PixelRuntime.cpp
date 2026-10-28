#include "PixelRuntime.h"
#include "PixelGame.h"
#include "Runtime/Render/PixelRender.h"

RenderSetup CreatePixelRenderSetup(PixelGame &game) {
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&game](DynamicRHI &rhi, IRenderSceneInterface *) {
        const auto character = game.GetArtAssets().LoadCharacter();
        return std::make_unique<PixelRender>(rhi, game.GetPresentation().LoadTextures(game.GetArtAssets()), game.CreateFramePublisher(character),
                                             "Pixel2DPostProcess.fs", game.GetArtAssets().LoadBackground("ArtAssets.json"), character, [&game]() -> const pixel::ui::Canvas & {
                                                 const auto &snapshot = game.GetPresentation().PublishedGameplay();
                                                 auto &ui = game.GetUI();
                                                 ui.Publish(snapshot ? &*snapshot : nullptr, game.GetSession().PlayerInventory());
                                                 return ui.Paint();
                                             });
    };
    return setup;
}
