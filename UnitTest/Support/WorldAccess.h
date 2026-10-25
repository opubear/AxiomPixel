#pragma once
#include "Runtime/Simulation/World/World.h"

namespace pixel::testing {
struct WorldAccess {
    static WorldMap &Map(pixel::World &world) { return world.Map; }
    static WorldRect &SimulationBounds(pixel::World &world) { return world.SimulationBounds; }
};
} // namespace pixel::testing
