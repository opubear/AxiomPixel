#pragma once

#include "Runtime/Gameplay/SimulationCoordinator.h"
#include "Runtime/Simulation/World/World.h"
#include <functional>

namespace pixel::testing {
// Test-only adapter for standalone World tests that need host-owned time.
class SimulatedWorld : public World {
  public:
    using World::World;

    uint32_t Tick(float duration, const std::function<void()> &onStep = {}, bool singleStep = false) {
        UpdateStreaming();
        if (singleStep)
            Coordinator.RequestStep();
        if (!Coordinator.BeginFrame(duration, {}))
            return 0;

        uint32_t completed = 0;
        while (Coordinator.HasStep()) {
            Coordinator.BeginStep();
            if (!StepEnvironment(Coordinator.Time()))
                break;
            Coordinator.CommitStep();
            ++completed;
            if (onStep)
                onStep();
            if (singleStep)
                break;
        }
        return completed;
    }

    double SimulationTime() const { return Coordinator.Time(); }
    double Remainder() const { return Coordinator.Remainder(); }

  private:
    SimulationCoordinator Coordinator;
};
} // namespace pixel::testing
