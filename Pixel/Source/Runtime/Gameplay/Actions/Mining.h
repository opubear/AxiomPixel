#pragma once
#include "Runtime/Gameplay/Items/Inventory.h"
#include "Runtime/Simulation/World/World.h"

namespace pixel {
// No collector means environmental excavation; player excavation always supplies one.
struct MiningCollector { items::Inventory &Inventory; items::ContainerId Container; };
enum class MiningStatus { Applied, Blocked, NotReady, InventoryFull };
struct MiningResult {
    MiningStatus Status;
    std::optional<items::ItemAmount> Yield;
};
std::optional<items::ItemAmount> MiningYield(CellTypeId material);
MiningResult MineCell(World &world, CellCoord cell, const std::optional<MiningCollector> &collector);
} // namespace pixel
