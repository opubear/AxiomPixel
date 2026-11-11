#include "PixelRuntime.h"
#include "PixelGame.h"
#include "Runtime/Render/PixelRender.h"

RenderSetup CreatePixelRenderSetup(PixelGame &game) {
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&game](DynamicRHI &rhi, IRenderSceneInterface *) {
        const auto character = game.GetArtAssets().LoadCharacter();
        const auto monsters = game.GetArtAssets().LoadMonsters();
        if (const auto *catalog = game.GetSession().GetMonsterCatalog())
            for (const auto &definition : catalog->Definitions) {
                const auto &visual = monsters->Require(definition.Id);
                if (definition.WakeTicks && (!visual.States.contains("Dormant") || !visual.States.contains("Wake")))
                    throw std::invalid_argument("ambush monster requires Dormant and Wake visuals: " + definition.Id);
            }
        return std::make_unique<PixelRender>(rhi, game.GetPresentation().LoadTextures(game.GetArtAssets()), game.CreateFramePublisher(character, monsters),
                                             "Pixel2DPostProcess.fs", game.GetArtAssets().LoadBackground("ArtAssets.json"), character, [&game]() -> const pixel::ui::Canvas & {
                                                 const auto &snapshot = game.GetPresentation().PublishedGameplay();
                                                 const auto &calendar = game.GetPresentation().PublishedCalendar();
                                                 auto &ui = game.GetUI();
                                                 ui.Publish(snapshot ? &*snapshot : nullptr, game.GetSession().PlayerInventory(), calendar ? &*calendar : nullptr);
                                                 return ui.Paint();
                                             }, monsters);
    };
    return setup;
}
