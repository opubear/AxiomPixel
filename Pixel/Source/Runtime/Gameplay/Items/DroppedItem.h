#pragma once
#include "Item.h"
#include "Runtime/Simulation/World/WorldCoordinates.h"

namespace pixel::items {
// The ground container owns the item; this record only locates it in the world.
struct DroppedItem {
    ItemRef Item;
    WorldPosition Position;
};
} // namespace pixel::items
