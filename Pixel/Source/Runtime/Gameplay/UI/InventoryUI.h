#pragma once

#include "Runtime/Gameplay/GameAPI/GameplayObservations.h"
#include "Runtime/Gameplay/Items/Inventory.h"
#include "UIScale.h"
#include "Canvas.h"
#include "Runtime/Simulation/World/Environment/WorldCalendar.h"
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pixel::ui {
// Logical pixels are shared by painting and hit testing; input supplies its own
// event's framebuffer extent so queued clicks retain their original DPI scale.
struct InventoryLayout {
    static constexpr int Width = 396, Height = 466, HotbarHeight = 108;
    static constexpr size_t Columns = 7, Rows = 5, SlotCount = Columns * Rows;
    static Rect QuickSlot(size_t index);
    static Rect BagSlot(size_t index);
    static Rect Placement(std::array<int, 2> framebuffer, float uiScale = 1);
};

struct Slot {
    std::optional<items::ItemId> Item;
    uint32_t Quantity = 0, Reserved = 0;
    uint64_t Instance = 0;
    std::optional<items::ItemRef> Reference() const {
        return Item && Quantity > Reserved ? std::optional(items::ItemRef{*Item, Instance}) : std::nullopt;
    }
    bool operator==(const Slot &) const = default;
};

struct InventoryView {
    bool Expanded = false;
    size_t Selected = 0;
    uint32_t Quantity = 0, Capacity = 0;
    std::string CalendarLabel;
    std::array<Slot, InventoryLayout::Columns> QuickSlots{};
    std::array<Slot, InventoryLayout::SlotCount> BagSlots{};
    bool operator==(const InventoryView &) const = default;
};

class InventoryUI {
  public:
    InventoryUI();
    float GetUIScale() const { return Image.UIScale; }
    void SetUIScale(float scale);
    void ToggleInventory();
    bool CloseInventory();
    bool IsExpanded() const { return View.Expanded; }
    void SelectQuickSlot(size_t index);
    // Coordinates already converted to framebuffer pixels by the input adapter.
    // Every button is consumed over a panel, only left presses change selection.
    bool PointerPress(float x, float y, std::array<int, 2> framebuffer, bool primary);
    void Publish(const api::GameplayObservation *gameplay, items::ContainerId playerBag,
                 const WorldCalendarSnapshot *calendar = nullptr);
    std::optional<items::ItemRef> HeldItem() const { return View.QuickSlots[View.Selected].Reference(); }
    const InventoryView &Read() const { return View; }
    const Canvas &Paint();

  private:
    InventoryView View;
    std::array<std::optional<items::ItemRef>, InventoryLayout::Columns> Bindings;
    std::vector<uint64_t> KnownInstances;
    uint64_t WorldEpoch = 0;
    items::ContainerId Container;
    std::string WorldId;
    Canvas Image;
    std::optional<InventoryView> Painted;
    void ResetBindings();
    void RefreshQuickSlots();
};
} // namespace pixel::ui
