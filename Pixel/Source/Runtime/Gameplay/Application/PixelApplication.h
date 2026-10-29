#pragma once

#include "Axiom.h"

// Application owns the game and debug UI; Engine owns their borrowed services.
class PixelApplication : public Axiom {
  public:
    PixelApplication() : Axiom("Pixel") {}

  protected:
    std::unique_ptr<IGameInterface> CreateGame() override;
    bool OnCloseRequested() override;
};
