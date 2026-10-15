#pragma once

#include "ForceField.h"
#include "Wind/WindSimulation.h"
#include "World/WorldState.h"
#include <cstddef>
#include <vector>

namespace pixel {
class World;

// Owns spatial base forces and wind. Material movement never transports fields.
// World owns this CPU-only environment and supplies material state for each step.
class Environment {
  public:
    performance::MemoryUsage GetMemoryUsage() const;

    Environment(uint32_t width, uint32_t height);
    ForceField &GetBaseForceField() { return BaseForces; }
    const ForceField &GetBaseForceField() const { return BaseForces; }
    void Tick(float duration, World &world);
    ForceVector SampleForce(uint32_t x, uint32_t y) const;
    WindSimulation &GetWindSimulation() { return Wind; }
    const WindSimulation &GetWindSimulation() const { return Wind; }

  private:
    struct CellMove {
        size_t Source;
        size_t Destination;
    };
    double TimeRemainder = 0;
    ForceField BaseForces;
    WindSimulation Wind;
    // Reused between steps; contain indices only, never borrowed cell storage.
    std::vector<uint8_t> Processed;
    std::vector<CellMove> MovementChain;
    std::vector<DirtMotion> DirtMovements;
    void StepForceField(World &world);
    void ResolveMovementChain(World &world, WorldStateView state, size_t source);
};
} // namespace pixel
