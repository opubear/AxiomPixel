#pragma once

#include "Lightmap.h"
#include <memory>

namespace pixel { struct WorldMapView; }

namespace pixel::render {
// Owned by a frame publisher. The background baker receives only a compact
// copy of one tile's dependencies and never holds a lease on the live world.
class LightingCache {
  public:
    LightingCache();
    ~LightingCache();
    void Build(WorldMapView world, WorldRect visible, std::optional<CellCoord> heldTorch, Lightmap &output);
    static uint64_t CapacityFor(WorldRect bounds);
    uint64_t ReservedBytes() const;
  private:
    struct State;
    std::unique_ptr<State> Cache;
};
} // namespace pixel::render
