#pragma once

#include "ForceField.h"
#include "Wind/WindSimulation.h"


namespace pixel {
class World;

class Environment {
  public:
    explicit Environment(WorldMap &map) : BaseForces(map) {}
    performance::MemoryUsage GetMemoryUsage() const;
    const ForceField &GetBaseForceField() const { return BaseForces; }
    // A retry preserves the already advanced wind and all material inputs.
    bool TryStep(World &world, double committedSeconds);
    ForceVector SampleForce(CellCoord cell) const;
    WindSimulation &GetWindSimulation() { return Wind; }
    const WindSimulation &GetWindSimulation() const { return Wind; }
    uint64_t LastVisitedCells() const { return VisitedCells; }
    uint64_t LastActiveRegions() const { return ActiveRegions; }
    bool IsWaitingForBudget() const { return WaitingForBudget; }
    void RestorePendingStep(double seconds, bool deferredStep);

  private:
    double PendingSeconds = 0;
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
