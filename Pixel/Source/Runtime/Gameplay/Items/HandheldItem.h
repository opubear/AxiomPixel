#pragma once
#include "Inventory.h"
#include "Runtime/Gameplay/GameAPI/Actions.h"
#include <functional>
#include <memory>
#include <stdexcept>

namespace pixel::items {
// The session supplies transactional world operations; items choose their use
// behavior without knowing about input, UI, rendering or the simulation worker.
struct ItemUseContext {
    std::function<api::ActionReceipt(uint32_t material)> PlaceBlockInFront;
    std::function<api::ActionReceipt(const ItemInstance &)> ActivateFormula;
};

class HandheldItem {
  public:
    virtual ~HandheldItem() = default;
    virtual api::ActionReceipt Use(const ItemUseContext &context) const = 0;
};

// One adapter for every block type. World cells and resource stacks remain data.
class BlockItem final : public HandheldItem {
  public:
    explicit BlockItem(uint32_t material) : Material(material) {}
    api::ActionReceipt Use(const ItemUseContext &context) const override {
        return context.PlaceBlockInFront(Material);
    }
  private:
    uint32_t Material;
};

class WoodenStick final : public HandheldItem {
  public:
    explicit WoodenStick(const ItemInstance &instance) : Instance(instance) {}
    api::ActionReceipt Use(const ItemUseContext &context) const override {
        return context.ActivateFormula(Instance);
    }
  private:
    const ItemInstance &Instance;
};

inline api::ProgramRef ItemProgram(const ItemInstance &item) {
    return {Inventory::InstanceResourceBase | item.Ref.Instance, item.FormulaVersion};
}

inline std::unique_ptr<HandheldItem> MakeHandheldItem(const Inventory &inventory, ContainerId bag, ItemRef item) {
    if (!inventory.ContainsItem(bag, item)) return {};
    if (item.Instance) {
        const auto *instance = inventory.FindInstance(bag, item);
        if (item.Item == ItemId::WoodenStick) return std::make_unique<WoodenStick>(*instance);
    } else if (const auto material = Definition(item.Item).BlockMaterial) {
        return std::make_unique<BlockItem>(material);
    }
    throw std::logic_error("owned item has no handheld behavior");
}
} // namespace pixel::items
