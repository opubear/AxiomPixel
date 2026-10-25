#include "Mining.h"

namespace pixel {
std::optional<items::ItemAmount> MiningYield(CellTypeId material) {
    using items::ItemId;
    switch (material) {
    case CellTypeId::Air: case CellTypeId::Water: return std::nullopt;
    case CellTypeId::Dirt: case CellTypeId::GroundSoil: case CellTypeId::GrassSoil: return {{ItemId::Soil, 1}};
    case CellTypeId::Stone: return {{ItemId::Stone, 1}};
    case CellTypeId::CoalOre: return {{ItemId::Coal, 1}};
    case CellTypeId::CopperOre: return {{ItemId::CopperOre, 1}};
    case CellTypeId::IronOre: return {{ItemId::IronOre, 1}};
    case CellTypeId::TungstenOre: return {{ItemId::TungstenOre, 1}};
    case CellTypeId::GoldOre: return {{ItemId::GoldOre, 1}};
    }
    throw std::logic_error("material has no mining definition");
}
MiningResult MineCell(World &world, CellCoord cell, const std::optional<MiningCollector> &collector) {
    const auto before = world.TryGetCell(cell.X, cell.Y);
    if (!before) return {MiningStatus::NotReady, {}};
    const auto yield = MiningYield(before->Type);
    if (!yield) return {MiningStatus::Blocked, {}};
    if (world.GetEnvironment().IsWaitingForBudget()) return {MiningStatus::NotReady, {}};
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
