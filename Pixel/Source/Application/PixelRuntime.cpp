#include "PixelRuntime.h"
#include "PixelGame.h"
#include "PixelRender/PixelRender.h"

RenderSetup CreatePixelRenderSetup(PixelGame &game) {
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&game](DynamicRHI &rhi, IRenderSceneInterface *) {
        return std::make_unique<PixelRender>(rhi, pixel::GamePresentation::MakeTextures(), game.CreateFramePublisher());
    };
    return setup;
}
