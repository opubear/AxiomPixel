#pragma once

#include "CellType.h"
#include <cstdint>
#include <span>
#include <vector>

namespace pixel {
// A direction in the cell's spatial force field, not a particle velocity.
// Positive Y points down; zero holds an object still. No magnitude is modeled.
struct ForceDirection {
    int32_t X = 0;
    int32_t Y = 1;
    bool operator==(const ForceDirection &) const = default;
    bool IsValid() const { return X >= -1 && X <= 1 && Y >= -1 && Y <= 1; }
};
struct CellState {
    CellTypeId Type = CellTypeId::Air;
    ForceDirection Force;
    bool operator==(const CellState &) const = default;
};
struct WorldStateView {
    uint32_t Width;
    uint32_t Height;
    std::span<const CellState> Cells;
};

// Fixed-size storage. Only World can write it; readers must not retain views
// across PublishState, which transfers the backing allocation to another owner.
class WorldState {
  public:
    WorldState(uint32_t width, uint32_t height);
    WorldState(const WorldState &) = delete;
    WorldState &operator=(const WorldState &) = delete;
    WorldState(WorldState &&) = delete;
    WorldState &operator=(WorldState &&) = delete;
    WorldStateView Read() const { return {Width, Height, Cells}; }

  private:
    friend class World;
    uint32_t Width;
    uint32_t Height;
    std::vector<CellState> Cells;
};
} // namespace pixel
