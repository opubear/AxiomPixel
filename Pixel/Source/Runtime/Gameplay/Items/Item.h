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
    Ice = 1110, Wood = 1120, Leaves = 1130, WoodenStick = 2001
};
struct ItemDefinition {
    ItemId Id;
    std::string_view Name;
    uint32_t Version = 1;
    // Canonical material placed by the shared block-item behavior; zero for tools.
    uint32_t BlockMaterial = 0;
};
struct ItemRef {
    ItemId Item = ItemId::Soil;
    uint64_t Instance = 0; // Zero addresses a resource stack, otherwise one owned tool.
    bool operator==(const ItemRef &) const = default;
};
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
