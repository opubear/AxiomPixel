#include "PixelApplication.h"
#include "PixelGame.h"

bool PixelApplication::OnCloseRequested() {
    return static_cast<PixelGame &>(*GetGame()).GetSession()
        .Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded();
}

std::unique_ptr<IGameInterface> PixelApplication::CreateGame() {
    auto game = std::make_unique<PixelGame>();
    game->ConfigurePerformanceCapture();
    return game;
}
