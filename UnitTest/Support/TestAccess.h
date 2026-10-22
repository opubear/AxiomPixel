#pragma once
#include "Gameplay/GameSession.h"
#include "WorldAccess.h"

namespace pixel::testing {
// Only test/benchmark sources include this header. Fixture mutation is kept out
// of Session and World's public gameplay interfaces.
struct SessionAccess {
    static pixel::World &World(GameSession &session) { return session.GameWorld; }
    static void RestoreClock(GameSession &session, double seconds, double remainder, bool deferred = false) {
        session.Simulation.Restore(seconds, remainder, deferred);
        session.GameWorld.RestorePendingEnvironmentStep(session.Simulation.Time(), deferred);
    }
    static void PublishFixtureChanges(GameSession &session) { session.SynchronizeReadViews(); }
    static FormulaRuntime &Runtime(GameSession &session) { return session.Formulas; }
    static WorldGenerationSettings &NextGeneration(GameSession &session) { return session.NextGeneration; }
};
} // namespace pixel::testing
