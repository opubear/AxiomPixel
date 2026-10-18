#pragma once

#include "Application/PixelGame.h"
#include "Axiom.h"

// Uses production game wiring without enabling environment-driven profiling.
class PixelSmokeApplication final : public Axiom {
  public:
    explicit PixelSmokeApplication(std::unique_ptr<PixelGame> game, AxiomSetup setup = {false})
        : PendingGame(std::move(game)) {
        Config = GameConfig::Load();
        Configure(setup);
    }

  protected:
    std::unique_ptr<IGameInterface> CreateGame() override { return std::move(PendingGame); }

  private:
    std::unique_ptr<PixelGame> PendingGame;
};
