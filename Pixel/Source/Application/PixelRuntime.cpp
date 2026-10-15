#include "PixelRuntime.h"
#include "PixelGame.h"
#include "Axiom.h"
#include "Engine.h"
#include "PixelRender/PixelRender.h"
#include "UI/Imgui/ImguiManager.h"

RenderSetup CreatePixelRenderSetup(PixelGame &game) {
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.EnableUI = true;
    setup.CreatePolicy = [&game](DynamicRHI &rhi) {
        auto render = std::make_unique<PixelRender>(rhi, pixel::GamePresentation::MakeTextures(), game.CreateFramePublisher());
        render->SetOverlay([](RHICommandList &commands) {
            GAxiom->Engine->GetImgui()->RecordDrawCommand(commands);
        });
        return render;
    };
    return setup;
}
