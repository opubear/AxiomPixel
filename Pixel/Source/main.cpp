#include "Axiom.h"
#include "Command/Command.h"
#include "PixelGame.h"
#include "PixelRender/PixelRender.h"
#include <iostream>

int main(int argc, char **argv) {
    try {
        const Command command(argc, argv);
        Axiom axiom;
        auto game = std::make_unique<PixelGame>();
        auto setup = game->CreateRenderSetup();
        return axiom.Run(command, std::move(game), std::move(setup));
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 64;
    }
}
