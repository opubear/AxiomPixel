#pragma once

#include "Runtime/Gameplay/GameAPI/GameplayObservations.h"
#include "Runtime/Gameplay/Items/Inventory.h"
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace pixel::ui {
struct Rect {
    float X = 0, Y = 0, Width = 0, Height = 0;
    bool Contains(float x, float y) const;
};

// Logical pixels are shared by painting and hit testing; input supplies its own
// event's framebuffer extent so queued clicks retain their original DPI scale.
struct InventoryLayout {
    static constexpr int Width = 396, Height = 466, HotbarHeight = 108;
    static constexpr size_t Columns = 7, Rows = 5, SlotCount = Columns * Rows;
    static Rect QuickSlot(size_t index);
    static Rect BagSlot(size_t index);
    static Rect Placement(std::array<int, 2> framebuffer);
};

struct Slot {
    std::optional<items::ItemId> Item;
    uint32_t Quantity = 0, Reserved = 0;
    bool operator==(const Slot &) const = default;
};

struct InventoryView {
    bool Expanded = false;
    size_t Selected = 0;
    uint32_t Quantity = 0, Capacity = 0;
    std::array<Slot, InventoryLayout::Columns> QuickSlots{};
    std::array<Slot, InventoryLayout::SlotCount> BagSlots{};
    bool operator==(const InventoryView &) const = default;
};

// Owned pixels, never references to the session or mutable inventory. The GPU
// consumer caches Revision per in-flight slot and draws after scene postprocessing.
struct Canvas {
    uint64_t Revision = 0;
    std::vector<std::byte> Pixels;
};

class InventoryUI {
  public:
    InventoryUI();
    void ToggleInventory();
    bool CloseInventory();
    bool IsExpanded() const { return View.Expanded; }
    void SelectQuickSlot(size_t index);
    // Coordinates already converted to framebuffer pixels by the input adapter.
    // Every button is consumed over a panel, only left presses change selection.
    bool PointerPress(float x, float y, std::array<int, 2> framebuffer, bool primary);
    void Publish(const api::GameplayObservation *gameplay, items::ContainerId playerBag);
    const InventoryView &Read() const { return View; }
    const Canvas &Paint();

  private:
    InventoryView View;
    std::array<std::optional<items::ItemId>, InventoryLayout::Columns> Bindings;
    uint64_t WorldEpoch = 0;
    items::ContainerId Container;
    std::string WorldId;
    Canvas Image;
    std::optional<InventoryView> Painted;
    void ResetBindings();
    void RefreshQuickSlots();
};
} // namespace pixel::ui
