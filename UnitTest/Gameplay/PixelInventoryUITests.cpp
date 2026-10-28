#include "Runtime/Gameplay/UI/InventoryUI.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace pixel;
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
api::GameplayObservation Snapshot() {
    api::GameplayObservation snapshot;
    snapshot.Stamp = {"ui-world", 1, 10, 2};
    api::InventoryObservation inventory;
    inventory.ContainerId = 12;
    inventory.Capacity = 4096;
    inventory.Quantity = 1025;
    inventory.Stacks = {{uint32_t(items::ItemId::Soil), 1, 1000, 20}, {uint32_t(items::ItemId::GoldOre), 1, 25, 0}};
    snapshot.Inventories.push_back(inventory);
    return snapshot;
}
void Click(ui::InventoryUI &ui, ui::Rect bounds, std::array<int, 2> size = {1280, 720}) {
    const auto placement = ui::InventoryLayout::Placement(size);
    const float scale = placement.Width / ui::InventoryLayout::Width;
    Require(ui.PointerPress(placement.X + (bounds.X + 24) * scale, placement.Y + (bounds.Y + 24) * scale, size, true),
        "slot did not consume click");
}
void InventoryAndBindings() {
    ui::InventoryUI ui;
    auto snapshot = Snapshot();
    ui.Publish(&snapshot, {12});
    Require(ui.Read().QuickSlots.size() == 7 && ui.Read().BagSlots.size() == 35 && !ui.IsExpanded(), "wrong initial layout");
    Require(ui.Read().QuickSlots[0].Quantity == 1000 && ui.Read().QuickSlots[0].Reserved == 20 && !ui.Read().BagSlots[7].Item,
        "UI did not project owned inventory values");
    ui.SelectQuickSlot(1);
    ui.ToggleInventory();
    Click(ui, ui::InventoryLayout::BagSlot(6));
    Require(ui.Read().QuickSlots[1].Item == items::ItemId::GoldOre && snapshot.Inventories[0].Quantity == 1025,
        "binding duplicated or mutated inventory");
    snapshot.Inventories[0].Stacks[1].Quantity = 0;
    ui.Publish(&snapshot, {12});
    Require(!ui.Read().QuickSlots[1].Item && ui.Read().QuickSlots[0].Quantity == 1000 && ui.IsExpanded(), "depletion moved other slots or closed UI");
    snapshot.Inventories[0].Stacks[1].Quantity = 8;
    ui.Publish(&snapshot, {12});
    Require(ui.Read().QuickSlots[1].Quantity == 8, "binding did not survive depletion");
    snapshot.Stamp.WorldEpoch++;
    ui.Publish(&snapshot, {12});
    Require(!ui.IsExpanded() && ui.Read().Selected == 0 && !ui.Read().QuickSlots[1].Item, "new world retained prior UI state");
    ui.Publish(nullptr, {12});
    Require(ui.Read().Quantity == 0 && !ui.Read().QuickSlots[0].Item, "missing publication retained stale inventory");
}
void LayoutAndCanvas() {
    ui::InventoryUI ui;
    auto snapshot = Snapshot();
    ui.Publish(&snapshot, {12});
    const auto revision = ui.Paint().Revision;
    Require(ui.Paint().Revision == revision, "unchanged UI regenerated canvas");
    const auto alpha = [&](int x, int y) { return std::to_integer<int>(ui.Paint().Pixels[size_t(y * ui::InventoryLayout::Width + x) * 4 + 3]); };
    Require(alpha(0, 0) == 255 && alpha(20, 200) == 0, "closed canvas has no hotbar or covers hidden bag");
    ui.ToggleInventory();
    Require(ui.Paint().Revision > revision && alpha(20, 200) == 255 && alpha(20, 112) == 0, "expanded canvas panels or gap incorrect");
    for (const auto size : {std::array{1280, 720}, std::array{2560, 1440}, std::array{320, 240}, std::array{800, 1200}}) {
        Click(ui, ui::InventoryLayout::QuickSlot(6), size);
        Require(ui.Read().Selected == 6, "DPI-scaled selection failed");
        Click(ui, ui::InventoryLayout::QuickSlot(0), size);
        const auto placement = ui::InventoryLayout::Placement(size);
        Require(placement.X + placement.Width < size[0] && placement.Y + placement.Height < size[1], "panel escapes viewport");
        Require(ui.PointerPress(placement.X + 2, placement.Y + 2, size, false), "panel padding leaked nonprimary click");
    }
    Require(!ui.PointerPress(5, 5, {0, 0}, true) && !ui.PointerPress(std::numeric_limits<float>::quiet_NaN(), 20, {1280, 720}, true),
        "invalid viewport or coordinates accepted");
    Require(ui.CloseInventory() && !ui.CloseInventory(), "close must report whether Escape was consumed");
    Require(!ui.PointerPress(30, 200, {1280, 720}, true), "hidden bag intercepted world click");
}
}
int main() {
    try {
        InventoryAndBindings();
        LayoutAndCanvas();
        std::cout << "Inventory UI state, bindings, canvas and scaled hit testing passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
