#include "InventoryUI.h"
#include "InventoryPainter.h"
#include <algorithm>
#include <cmath>

namespace pixel::ui {
bool Rect::Contains(float x, float y) const {
    return x >= X && y >= Y && x < X + Width && y < Y + Height;
}

Rect InventoryLayout::QuickSlot(size_t index) { return {12.f + float(index) * 54, 30, 48, 48}; }
Rect InventoryLayout::BagSlot(size_t index) {
    return {12.f + float(index % Columns) * 54, 162.f + float(index / Columns) * 54, 48, 48};
}
Rect InventoryLayout::Placement(std::array<int, 2> framebuffer) {
    if (framebuffer[0] <= 0 || framebuffer[1] <= 0)
        return {};
    const float scale = std::min({2.f, float(framebuffer[0]) / 1280, float(framebuffer[1]) / 720});
    return {16 * scale, 16 * scale, Width * scale, Height * scale};
}

InventoryUI::InventoryUI() { ResetBindings(); }

void InventoryUI::ResetBindings() {
    Bindings.fill(std::nullopt);
    const auto definitions = items::Definitions();
    for (size_t i = 0; i < std::min(Bindings.size(), definitions.size()); ++i)
        Bindings[i] = definitions[i].Id;
    View.Selected = 0;
}

void InventoryUI::ToggleInventory() { View.Expanded = !View.Expanded; }
bool InventoryUI::CloseInventory() {
    if (!View.Expanded)
        return false;
    View.Expanded = false;
    return true;
}
void InventoryUI::SelectQuickSlot(size_t index) {
    if (index < View.QuickSlots.size())
        View.Selected = index;
}

void InventoryUI::RefreshQuickSlots() {
    View.QuickSlots.fill({});
    for (size_t i = 0; i < Bindings.size(); ++i)
        for (const auto &slot : View.BagSlots)
            if (Bindings[i] && slot.Item == Bindings[i]) {
                View.QuickSlots[i] = slot;
                break;
            }
}

bool InventoryUI::PointerPress(float x, float y, std::array<int, 2> framebuffer, bool primary) {
    const auto placement = InventoryLayout::Placement(framebuffer);
    if (!placement.Width || !std::isfinite(x) || !std::isfinite(y))
        return false;
    x = (x - placement.X) * InventoryLayout::Width / placement.Width;
    y = (y - placement.Y) * InventoryLayout::Height / placement.Height;
    const bool inHotbar = Rect{0, 0, InventoryLayout::Width, InventoryLayout::HotbarHeight}.Contains(x, y);
    const bool inBag = View.Expanded && Rect{0, 120, InventoryLayout::Width, InventoryLayout::Height - 120}.Contains(x, y);
    if (!inHotbar && !inBag)
        return false;
    if (!primary)
        return true;
    for (size_t i = 0; i < View.QuickSlots.size(); ++i)
        if (InventoryLayout::QuickSlot(i).Contains(x, y))
            SelectQuickSlot(i);
    if (inBag)
        for (size_t i = 0; i < View.BagSlots.size(); ++i)
            if (InventoryLayout::BagSlot(i).Contains(x, y) && View.BagSlots[i].Item) {
                Bindings[View.Selected] = View.BagSlots[i].Item;
                RefreshQuickSlots();
            }
    return true;
}

void InventoryUI::Publish(const api::GameplayObservation *gameplay, items::ContainerId playerBag) {
    // A missing publication never reads mutable live inventory as a substitute.
    if (gameplay && (gameplay->Stamp.WorldEpoch != WorldEpoch || gameplay->Stamp.WorldId != WorldId || Container != playerBag)) {
        WorldEpoch = gameplay->Stamp.WorldEpoch;
        WorldId = gameplay->Stamp.WorldId;
        Container = playerBag;
        ResetBindings();
        View.Expanded = false;
    }
    View.BagSlots.fill({});
    View.Capacity = View.Quantity = 0;
    if (gameplay) {
        const auto bag = std::find_if(gameplay->Inventories.begin(), gameplay->Inventories.end(),
            [playerBag](const auto &entry) { return entry.ContainerId == playerBag.Value; });
        if (bag != gameplay->Inventories.end()) {
            View.Capacity = bag->Capacity;
            View.Quantity = bag->Quantity;
            // The current domain stores resource totals, not owned slot stacks.
            // Stable definition positions avoid moving items when a count hits 0.
            const auto definitions = items::Definitions();
            for (size_t i = 0; i < std::min(definitions.size(), View.BagSlots.size()); ++i)
                for (const auto &stack : bag->Stacks)
                    if (stack.ItemId == uint32_t(definitions[i].Id) && stack.Quantity)
                        View.BagSlots[i] = {definitions[i].Id, stack.Quantity, stack.Reserved};
        }
    }
    RefreshQuickSlots();
}

const Canvas &InventoryUI::Paint() {
    if (!Painted || *Painted != View) {
        PaintInventory(View, Image.Pixels);
        ++Image.Revision;
        Painted = View;
    }
    return Image;
}
} // namespace pixel::ui
