#pragma once
#include <cstdint>
#include <span>
#include <string_view>

namespace pixel::items {
// Content identities are stable semantic values, never texture layers or slots.
enum class ItemId : uint32_t {
    Soil = 1001, Stone = 1010, Coal = 1020, CopperOre = 1030,
    IronOre = 1040, TungstenOre = 1050, GoldOre = 1060,
    Sand = 1070, Clay = 1080, Sandstone = 1090, Snow = 1100,
    Ice = 1110, Wood = 1120, Leaves = 1130
};
struct ItemDefinition { ItemId Id; std::string_view Name; uint32_t Version = 1; };
struct ItemAmount {
    ItemId Item;
    uint32_t Quantity = 0;
    bool operator==(const ItemAmount &) const = default;
};
std::span<const ItemDefinition> Definitions();
bool IsKnown(ItemId item);
const ItemDefinition &Definition(ItemId item);
// Content assembly is trusted: invalid references/duplicates fail at startup.
void ValidateDefinitions(std::span<const ItemDefinition> definitions);
void ValidateCost(std::span<const ItemAmount> cost);
} // namespace pixel::items
