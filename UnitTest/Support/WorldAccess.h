#pragma once
#include "Runtime/Simulation/World/World.h"

namespace pixel::testing {
struct WorldAccess {
    static WorldMap &Map(pixel::World &world) { return world.GetMutableMap(); }
    static WorldRect &SimulationBounds(pixel::World &world) { return world.SimulationBounds; }
    // Fork/recycle the writer before measuring remaining capacity: the swap can
    // release obsolete chunks, so measuring the sealed slot creates false pressure.
    static void SaturateBudget(pixel::World &world, uint64_t remaining = 128) {
        Map(world);
        const auto *stream = world.GetStreamer();
        const auto retained = stream->RetainedBytes();
        if (retained + remaining > stream->Settings().BudgetBytes)
            throw std::logic_error("budget fixture already exceeds capacity");
        world.SetPublicationBytes(world.PublicationBytesFor(nullptr) + stream->Settings().BudgetBytes - retained - remaining);
    }
};
} // namespace pixel::testing
