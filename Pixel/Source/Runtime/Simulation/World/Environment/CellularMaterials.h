#pragma once

#include "Runtime/Simulation/World/WorldMap.h"
#include "Wind/WindTypes.h"

namespace pixel::cellular {
// These are visual motion rules. Every occupied cell is one material; there
// are no pressure, heat, oxygen, or per-particle physics fields.
struct Update {
    CellTypeId Material;
    std::optional<CellCoord> Destination;
};

constexpr bool IsTransient(CellTypeId type) {
    return type == CellTypeId::Steam || type == CellTypeId::Fire;
}

Update Evaluate(const WorldMap &map, WorldRect bounds, CellCoord cell,
                CellTypeId material, uint64_t step, WindVelocity wind);
} // namespace pixel::cellular
