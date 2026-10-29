#pragma once

#include "Actors.h"
#include "Runtime/Gameplay/SimulationCoordinator.h"
#include "Runtime/Gameplay/GameAPI/CommitStamp.h"
#include "Runtime/Simulation/World/WorldBuffers.h"

namespace pixel {
// Game-owned visual motion. Collision uses a retained completed world, never
// the mutable solver. The next game frame rebases on authoritative actor state.
struct PlayerPresentation {
    api::CommitStamp Base;
    std::optional<ActorSnapshot> Player;
    std::optional<Body> Motion;
    WorldBuffers::ReadLease Terrain;
    SimulationCoordinator Clock;
    bool Advancing = false;
    double Time = 0;

    void Advance(float duration, PlayerInput input) {
        if (!Advancing || !Player || !Motion || !Terrain || !Clock.BeginFrame(duration, input)) return;
        const auto terrain = Terrain->Read(Terrain->SimulationBounds());
        while (Clock.HasStep()) {
            const auto intent = Clock.BeginStep();
            if (Motion->HasTerrain(terrain))
                Motion->Tick(float(SimulationCoordinator::StepSeconds), terrain, intent.Horizontal, intent.Jump);
            if (Clock.CommitStep()) break;
        }
        Player->Position = Motion->GetWorldPosition();
        Player->Grounded = Motion->Grounded;
        Time = Clock.Time();
    }
};
} // namespace pixel
