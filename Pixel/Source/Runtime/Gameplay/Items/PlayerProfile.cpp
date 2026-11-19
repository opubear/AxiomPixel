#include "PlayerProfile.h"
#include "BundledPlayerProfiles.h"
#include "Inventory.h"
#include <algorithm>
#include <bitset>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace pixel::items {
namespace {
uint32_t PositiveInteger(const nlohmann::json &entry, const char *field, uint32_t maximum) {
    const auto &value = entry.at(field);
    if (!value.is_number_integer() || value < 1 || value > maximum)
        throw std::invalid_argument(std::string(field) + " must be an integer in [1," + std::to_string(maximum) + "]");
    return value.get<uint32_t>();
}
} // namespace

const PlayerProfile &PlayerProfile::BundledNormal() {
    static const auto profile = Parse(BundledPlayerProfiles, "normal");
    return profile;
}

PlayerProfile PlayerProfile::Parse(std::string_view document, std::string_view selected) {
    const auto json = nlohmann::json::parse(document);
    const auto &profiles = json.at("Profiles");
    const auto name = selected.empty() ? json.at("DefaultProfile").get<std::string>() : std::string(selected);
    if (!profiles.is_object() || !profiles.contains(name))
        throw std::invalid_argument("unknown player profile: " + name);
    const auto &inventory = profiles.at(name).at("StartingInventory");
    if (!inventory.is_array() || inventory.size() > BagSlotCount)
        throw std::invalid_argument("invalid StartingInventory for player profile: " + name);

    PlayerProfile profile;
    std::bitset<BagSlotCount> bagSlots;
    std::bitset<QuickSlotCount> quickSlots;
    size_t tools = 0;
    uint32_t total = 0;
    const auto definitions = Definitions();
    for (const auto &entry : inventory) {
        const auto item = entry.at("Item").get<std::string>();
        const auto definition = std::ranges::find(definitions, item, &ItemDefinition::Name);
        if (definition == definitions.end())
            throw std::invalid_argument("unknown starting item: " + item);

        const size_t bag = PositiveInteger(entry, "BagSlot", BagSlotCount) - 1;
        if (bagSlots.test(bag)) throw std::invalid_argument("duplicate starting BagSlot");
        bagSlots.set(bag);
        std::optional<size_t> quick;
        if (entry.contains("QuickSlot")) {
            quick = PositiveInteger(entry, "QuickSlot", QuickSlotCount) - 1;
            if (quickSlots.test(*quick)) throw std::invalid_argument("duplicate starting QuickSlot");
            quickSlots.set(*quick);
        }

        const auto quantity = PositiveInteger(entry, "Quantity", BagCapacity);
        total += quantity;
        if (total > BagCapacity) throw std::invalid_argument("starting inventory exceeds player bag capacity");
        if (definition->BlockMaterial) {
            if (std::ranges::any_of(profile.Entries, [&](const StartingItem &other) { return other.Amount.Item == definition->Id; }))
                throw std::invalid_argument("duplicate starting stack: " + item);
        } else {
            if (quantity != 1) throw std::invalid_argument("starting tools require one instance per bag slot");
            if (++tools > Inventory::MaximumInstancesPerContainer)
                throw std::invalid_argument("starting inventory exceeds the player bag tool limit");
        }
        profile.Entries.push_back({{definition->Id, quantity}, bag, quick});
    }
    // Create tool instances in bag order so their stable identities reproduce the same layout after loading.
    std::ranges::sort(profile.Entries, {}, &StartingItem::BagSlot);
    return profile;
}
} // namespace pixel::items
