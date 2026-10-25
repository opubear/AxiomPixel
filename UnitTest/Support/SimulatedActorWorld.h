#pragma once
#include "SimulatedWorld.h"
#include "Runtime/Gameplay/Actors/Actors.h"

namespace pixel::testing {
// Fixture composition for character/terrain integration, not a World ownership API.
class SimulatedActorWorld : public SimulatedWorld {
  public:
    using SimulatedWorld::SimulatedWorld;
    const Body *GetPlayer() const { return Characters.Player(); }
    void SpawnPlayer(double x, double y) { RestorePlayer(WorldPosition::FromAbsolute(x, y)); }
    void RestorePlayer(WorldPosition position) {
        Characters.SetPlayerPosition(position);
        SetSimulationFocus(position);
        UpdateStreaming();
        Input = {};
    }
    void SetPlayerInput(int horizontal, bool jump) { Input = {horizontal, Input.Jump || jump}; }
    void ClearPlayerInput() { Input = {}; }
    uint32_t Tick(float duration, const std::function<void()> &onStep = {}, bool singleStep = false) {
        if (GetPlayer()) SetSimulationFocus(GetPlayer()->GetWorldPosition());
        return SimulatedWorld::Tick(duration, [&] {
            Characters.Step(ReadState(), Input);
            Input.Jump = false;
            if (onStep) onStep();
        }, singleStep);
    }
    CellEditResult TryPlaceDirt(int64_t x, int64_t y) {
        return Characters.Occupies({x, y}) ? CellEditResult::Blocked : World::TryPlaceDirt(x, y);
    }
    bool PlaceDirt(int64_t x, int64_t y) { return TryPlaceDirt(x, y) == CellEditResult::Applied; }

  private:
    pixel::Actors Characters;
    PlayerInput Input;
};
} // namespace pixel::testing
