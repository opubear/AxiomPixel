#include "PixelApplication.h"
#include "PixelGame.h"

std::unique_ptr<IGameInterface> PixelApplication::CreateGame() {
    auto game = std::make_unique<PixelGame>();
    game->ConfigurePerformanceCapture();
    return game;
}
