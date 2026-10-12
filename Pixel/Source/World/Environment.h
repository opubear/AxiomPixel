#pragma once

#include "ForceField.h"
#include "WindSimulation.h"
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
    ForceVector SampleForce(ForceVector base, uint32_t x, uint32_t y) const;
    WindSimulation &GetWindSimulation() { return Wind; }
    const WindSimulation &GetWindSimulation() const { return Wind; }

  private:
    struct CellMove {
        size_t Source;
        size_t Destination;
    };
    double TimeRemainder = 0;
    WindSimulation Wind;
    // Reused between steps; contain indices only, never borrowed cell storage.
    std::vector<uint8_t> Processed;
    std::vector<CellMove> MovementChain;
    std::vector<DirtMotion> DirtMovements;
    void StepForceField(World &world);
    void ResolveMovementChain(World &world, WorldStateView state, size_t source);
};
} // namespace pixel
