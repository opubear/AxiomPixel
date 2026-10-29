#pragma once
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "WorldAccess.h"

namespace pixel::testing {
// Only test/benchmark sources include this header. Fixture mutation is kept out
// of Session and World's public gameplay interfaces.
struct SessionAccess {
    static pixel::World &World(GameSession &session) { return session.GameWorld; }
    static pixel::Actors &Actors(GameSession &session) { return session.Characters; }
    static pixel::Devices &Devices(GameSession &session) { return session.Machines; }
    static items::Inventory &Inventory(GameSession &session) { return session.Inventory; }
    static void SetPlayerPosition(GameSession &session, WorldPosition position) {
        session.Characters.SetPlayerPosition(position);
        session.GameWorld.SetSimulationFocus(position);
        session.GameWorld.UpdateStreaming();
    }
    static void RestoreClock(GameSession &session, double seconds, double remainder, bool deferred = false) {
        session.Simulation.Restore(seconds, remainder, deferred);
        session.GameWorld.RestorePendingEnvironmentStep(session.Simulation.Time(), deferred);
    }
    static void PublishFixtureChanges(GameSession &session) { session.SynchronizeReadViews(); }
    // Direct fixture mutations such as Inventory.Add bypass domain revision
    // detection. Publish them explicitly at an already complete step boundary.
    static void CommitFixtureChanges(GameSession &session) { session.CommitChange(); }
    static FormulaBridge &Runtime(GameSession &session) { return session.Formulas; }
    static WorldGenerationSettings &NextGeneration(GameSession &session) { return session.NextGeneration; }
    // Stop between applying commands and the environment step to inject budget
    // pressure without relying on scheduler timing or production test hooks.
    static void PrepareActions(GameSession &session) {
        session.Worker->Invoke([&] { session.ApplyPendingActions(); });
    }
    static const api::ActionReceipt &PreparedAction(GameSession &session) { return *session.PendingActions.front().Result; }
    static bool HasQueuedChanges(const GameSession &session) {
        return !session.PendingActions.empty() || session.DeferredPublication;
    }
    static void FinishQueuedChanges(GameSession &session) {
        if (!HasQueuedChanges(session)) return;
        session.Control(NativeCaller::Driver, api::StepSimulation{});
        session.Tick(1.f / 60, {});
    }
    static api::ActionReceipt ExecuteAndStep(GameSession &session, NativeCaller caller, api::Action action) {
        auto receipt = session.Execute(caller, std::move(action));
        if (!receipt.Queued()) return receipt;
        FinishQueuedChanges(session);
        return *session.FindAction(receipt.Key);
    }
};
} // namespace pixel::testing
