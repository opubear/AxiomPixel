#pragma once
#include "World/World.h"

namespace pixel::testing {
struct WorldAccess {
    static WorldMap &Map(pixel::World &world) { return world.Map; }
};
} // namespace pixel::testing
