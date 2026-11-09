#include "PixelApplication.h"
#include "PixelGame.h"
#include "DisplaySettings.h"
#include "Asset/Loaders/AssetLoader.h"

bool PixelApplication::OnCloseRequested() {
    auto &session = static_cast<PixelGame &>(*GetGame()).GetSession();
    return !session.IsPersistent()
        || session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded();
}

std::unique_ptr<IGameInterface> PixelApplication::CreateGame() {
    const auto text = AssetLoader::LoadConfigFile("GameConfig.json");
    const auto settings = pixel::DisplaySettings::Parse({text.data(), text.size()});
    auto game = std::make_unique<PixelGame>();
    game->SetSceneScale(settings.SceneScale);
    game->SetUIScale(settings.UIScale);
    return game;
}
