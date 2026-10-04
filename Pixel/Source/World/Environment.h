#pragma once

#include "WindField.h"
#include "WorldState.h"
#include <cstddef>
#include <vector>

namespace pixel {
class World;

// The per-cell force field belongs to spatial positions in World. Environment
// adds optional simulated wind and moves material without transporting the field.
class Environment {
  public:
    void Tick(float duration, World &world);
    // Continuous field before the movement dead zone: base direction * 60 + wind.
    WindVelocity SampleFieldVelocity(ForceDirection base, float x, float y) const;
    WindField &GetWind() { return Wind; }
    const WindField &GetWind() const { return Wind; }

  private:
    struct CellMove {
        size_t Source;
        size_t Destination;
    };
    double TimeRemainder = 0;
    WindField Wind;
    // Reused between steps; contain indices only, never borrowed cell storage.
    std::vector<uint8_t> Processed;
    std::vector<CellMove> MovementChain;
    std::vector<DirtMotion> DirtMovements;
    void StepForceField(World &world);
    void ResolveMovementChain(World &world, WorldStateView state, size_t source);
};
} // namespace pixel
