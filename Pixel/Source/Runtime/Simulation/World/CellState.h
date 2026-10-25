#pragma once

#include "CellType.h"
#include <cstdint>

namespace pixel {
struct ForceDirection {
    int32_t X = 0, Y = 1;
    bool operator==(const ForceDirection &) const = default;
    bool IsValid() const { return X >= -1 && X <= 1 && Y >= -1 && Y <= 1; }
};

struct CellState {
    CellTypeId Type = CellTypeId::Air;
    ForceDirection Force;
    bool operator==(const CellState &) const = default;
};

struct ForceVector {
    float X = 0, Y = 0;
    bool operator==(const ForceVector &) const = default;
};
} // namespace pixel
