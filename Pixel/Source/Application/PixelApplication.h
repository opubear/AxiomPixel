#pragma once

#include "Axiom.h"

// Application owns the game and debug UI; Engine owns their borrowed services.
class PixelApplication final : public Axiom {
  protected:
    std::unique_ptr<IGameInterface> CreateGame() override;
};
