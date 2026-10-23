#pragma once
#include "Items/Item.h"
#include <array>

namespace pixel {
// First native construction recipe, version 1. Placement belongs to M4.3.
inline std::span<const items::ItemAmount> ForceEmitterCost() {
    static constexpr std::array cost{items::ItemAmount{items::ItemId::Stone, 4},
                                     items::ItemAmount{items::ItemId::CopperOre, 2}};
    return cost;
}
} // namespace pixel
