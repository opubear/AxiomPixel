#pragma once
#include "Gameplay/GameSession.h"
#include "WorldAccess.h"

namespace pixel::testing {
// Only test/benchmark sources include this header. Fixture mutation is kept out
// of Session and World's public gameplay interfaces.
struct SessionAccess {
    static pixel::World &World(GameSession &session) { return session.GameWorld; }
    static FormulaRuntime &Runtime(GameSession &session) { return session.Formulas; }
    static WorldGenerationSettings &NextGeneration(GameSession &session) { return session.NextGeneration; }
};
} // namespace pixel::testing
