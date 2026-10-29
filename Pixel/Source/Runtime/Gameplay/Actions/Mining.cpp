#include "Mining.h"

namespace pixel {
std::optional<items::ItemAmount> MiningYield(CellTypeId material) {
    using items::ItemId;
    switch (material) {
    case CellTypeId::Air: case CellTypeId::Water: case CellTypeId::Steam: case CellTypeId::Fire: return std::nullopt;
    case CellTypeId::Dirt: case CellTypeId::GroundSoil: case CellTypeId::GrassSoil: return {{ItemId::Soil, 1}};
    case CellTypeId::MossStone: case CellTypeId::Stone: return {{ItemId::Stone, 1}};
    case CellTypeId::CoalOre: return {{ItemId::Coal, 1}};
    case CellTypeId::CopperOre: return {{ItemId::CopperOre, 1}};
    case CellTypeId::IronOre: return {{ItemId::IronOre, 1}};
    case CellTypeId::TungstenOre: return {{ItemId::TungstenOre, 1}};
    case CellTypeId::GoldOre: return {{ItemId::GoldOre, 1}};
    case CellTypeId::Sand: return {{ItemId::Sand, 1}};
    case CellTypeId::Clay: return {{ItemId::Clay, 1}};
    case CellTypeId::Sandstone: return {{ItemId::Sandstone, 1}};
    case CellTypeId::Snow: return {{ItemId::Snow, 1}};
    case CellTypeId::Ice: return {{ItemId::Ice, 1}};
    case CellTypeId::Wood: return {{ItemId::Wood, 1}};
    case CellTypeId::Leaves: return {{ItemId::Leaves, 1}};
    }
    throw std::logic_error("material has no mining definition");
}
MiningResult MineCell(World &world, CellCoord cell, const std::optional<MiningCollector> &collector) {
    const auto before = world.TryGetCell(cell.X, cell.Y);
    if (!before) return {MiningStatus::NotReady, {}};
    const auto yield = MiningYield(before->Type);
    if (!yield) return {MiningStatus::Blocked, {}};
    if (world.GetEnvironment().IsWaitingForBudget()) return {MiningStatus::NotReady, {}};
    if (!world.TryPrepareMaterialWrite({cell.X, cell.Y, 1, 1})) return {MiningStatus::NotReady, {}};
    if (collector) {
        const auto added = collector->Inventory.Add(collector->Inventory.Reference(collector->Container), {&*yield, 1});
        if (added == items::Result::CapacityExceeded) return {MiningStatus::InventoryFull, {}};
        if (added != items::Result::Applied) throw std::logic_error("invalid mining inventory configuration");
    }
    // No callbacks or queries can interleave this preflighted two-domain commit.
    if (world.TryRemoveMaterial(cell.X, cell.Y) != CellEditResult::Applied)
        throw std::logic_error("preflighted mining failed");
    return {MiningStatus::Applied, collector ? yield : std::nullopt};
}
} // namespace pixel
