#include "PixelRuntime.h"
#include "PixelGame.h"
#include "PixelRender/PixelRender.h"

RenderSetup CreatePixelRenderSetup(PixelGame &game) {
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&game](DynamicRHI &rhi, IRenderSceneInterface *) {
        return std::make_unique<PixelRender>(rhi, game.GetPresentation().LoadTextures(game.GetArtAssets()), game.CreateFramePublisher(),
                                             "Pixel2DPostProcess.fs", game.GetArtAssets().LoadBackground("ArtAssets.json"));
    };
    return setup;
}
