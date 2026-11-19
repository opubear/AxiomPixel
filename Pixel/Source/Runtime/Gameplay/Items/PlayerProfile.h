#pragma once

#include "Item.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace pixel::items {
struct StartingItem {
    ItemAmount Amount;
    size_t BagSlot; // Zero-based after parsing the player-facing slot numbers.
    std::optional<size_t> QuickSlot;
};

class PlayerProfile {
  public:
    static constexpr uint32_t BagCapacity = 4096;
    static constexpr size_t BagSlotCount = 42, QuickSlotCount = 7;

    static PlayerProfile Parse(std::string_view document, std::string_view selected = {});
    static const PlayerProfile &BundledNormal();
    std::span<const StartingItem> StartingInventory() const { return Entries; }

  private:
    std::vector<StartingItem> Entries;
};
} // namespace pixel::items
