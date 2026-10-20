#pragma once

#include "ForceField.h"
#include "Wind/WindSimulation.h"
#include <functional>

namespace pixel {
class World;

class Environment {
  public:
    explicit Environment(WorldMap &map) : BaseForces(map) {}
    performance::MemoryUsage GetMemoryUsage() const;
    const ForceField &GetBaseForceField() const { return BaseForces; }
    // Notifications run only after a complete material/wind step commits.
    uint32_t Tick(float duration, World &world, const std::function<void()> &onStep = {}, bool singleStep = false);
    ForceVector SampleForce(CellCoord cell) const;
    WindSimulation &GetWindSimulation() { return Wind; }
    const WindSimulation &GetWindSimulation() const { return Wind; }
    uint64_t LastVisitedCells() const { return VisitedCells; }
    uint64_t LastActiveRegions() const { return ActiveRegions; }
    bool IsWaitingForBudget() const { return WaitingForBudget; }
    double GetSimulationTime() const { return SimulationSeconds; }
    double GetTimeRemainder() const { return TimeRemainder; }
    void RestoreClock(double seconds, double remainder, bool deferredStep = false);

  private:
    double TimeRemainder = 0;
    double SimulationSeconds = 0;
    ForceField BaseForces;
    WindSimulation Wind;
    axiom::Array<MaterialMove> Proposals;
    axiom::Array<uint32_t> Winners, WinnerEpochs;
    uint32_t Epoch = 0;
    ForceDirection ProposalDirection;
    bool SharedProposalDirection = true;
    axiom::Array<DirtMotion> DirtMovements;
    uint64_t VisitedCells = 0;
    uint64_t ActiveRegions = 0;
    bool WindAdvancedForStep = false;
    bool WaitingForBudget = false;
    void StepMaterials(World &world);
    void ProposeRegion(World &world, WorldRect region);
    void ResolveConflicts(World &world, WorldRect bounds);
};
} // namespace pixel
