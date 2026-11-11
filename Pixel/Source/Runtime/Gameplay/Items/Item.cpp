#include "Item.h"
#include "Runtime/Simulation/World/CellType.h"
#include <array>
#include <stdexcept>

namespace pixel::items {
std::span<const ItemDefinition> Definitions() {
    static constexpr std::array values{
        ItemDefinition{ItemId::Soil, "soil", 1, uint32_t(CellTypeId::Dirt)}, ItemDefinition{ItemId::Stone, "stone", 1, uint32_t(CellTypeId::Stone)},
        ItemDefinition{ItemId::Coal, "coal", 1, uint32_t(CellTypeId::CoalOre)}, ItemDefinition{ItemId::CopperOre, "copper_ore", 1, uint32_t(CellTypeId::CopperOre)},
        ItemDefinition{ItemId::IronOre, "iron_ore", 1, uint32_t(CellTypeId::IronOre)}, ItemDefinition{ItemId::TungstenOre, "tungsten_ore", 1, uint32_t(CellTypeId::TungstenOre)},
        ItemDefinition{ItemId::GoldOre, "gold_ore", 1, uint32_t(CellTypeId::GoldOre)},
        ItemDefinition{ItemId::Sand, "sand", 1, uint32_t(CellTypeId::Sand)},
        ItemDefinition{ItemId::Clay, "clay", 1, uint32_t(CellTypeId::Clay)},
        ItemDefinition{ItemId::Sandstone, "sandstone", 1, uint32_t(CellTypeId::Sandstone)},
        ItemDefinition{ItemId::Snow, "snow", 1, uint32_t(CellTypeId::Snow)},
        ItemDefinition{ItemId::Ice, "ice", 1, uint32_t(CellTypeId::Ice)},
        ItemDefinition{ItemId::Wood, "wood", 1, uint32_t(CellTypeId::Wood)},
        ItemDefinition{ItemId::Leaves, "leaves", 1, uint32_t(CellTypeId::Leaves)},
        ItemDefinition{ItemId::WoodenStick, "wooden_stick"}};
    return values;
}
bool IsKnown(ItemId item) {
    for (const auto &entry : Definitions()) if (entry.Id == item) return true;
    return false;
}
const ItemDefinition &Definition(ItemId item) {
    for (const auto &entry : Definitions()) if (entry.Id == item) return entry;
    throw std::logic_error("unknown item content reference");
}
void ValidateDefinitions(std::span<const ItemDefinition> definitions) {
    for (size_t i = 0; i < definitions.size(); ++i) {
        const auto &entry = definitions[i];
        if (uint32_t(entry.Id) == 0 || entry.Name.empty() || entry.Version == 0)
            throw std::logic_error("invalid item definition");
        for (size_t j = 0; j < i; ++j)
            if (definitions[j].Id == entry.Id || definitions[j].Name == entry.Name)
                throw std::logic_error("duplicate item definition");
    }
}
void ValidateCost(std::span<const ItemAmount> cost) {
    if (cost.empty()) throw std::logic_error("empty construction cost");
    for (size_t i = 0; i < cost.size(); ++i) {
        Definition(cost[i].Item);
        if (!cost[i].Quantity) throw std::logic_error("zero construction cost");
        for (size_t j = 0; j < i; ++j)
            if (cost[j].Item == cost[i].Item) throw std::logic_error("duplicate construction cost item");
    }
}
} // namespace pixel::items
