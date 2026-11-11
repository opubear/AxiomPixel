#include "InventoryPainter.h"
#include "CanvasPainter.h"
#include <algorithm>
#include <string>
#include <string_view>

namespace pixel::ui {
namespace {
using namespace palette;

class Painter : public CanvasPainter {
  public:
    explicit Painter(std::vector<std::byte> &pixels) : CanvasPainter(pixels, InventoryLayout::Width, InventoryLayout::Height) {}

    void Icon(int x, int y, items::ItemId item) {
        if (item == items::ItemId::WoodenStick) {
            for (int i = 0; i < 22; ++i) {
                Fill(x + 3 + i / 2, y + 24 - i, 5, 2, {91, 53, 26, 255});
                Fill(x + 4 + i / 2, y + 24 - i, 2, 1, {192, 139, 73, 255});
            }
            Fill(x + 12, y + 7, 7, 2, {229, 185, 106, 255});
            return;
        }
        Color base{115, 124, 127, 255}, light{179, 186, 177, 255};
        switch (item) {
        case items::ItemId::Soil: base = {113,77,49,255}; light = {177,124,73,255}; break;
        case items::ItemId::Coal: base = {51,58,66,255}; light = {107,117,126,255}; break;
        case items::ItemId::CopperOre: light = {223,139,77,255}; break;
        case items::ItemId::IronOre: light = {201,169,145,255}; break;
        case items::ItemId::TungstenOre: light = {139,194,210,255}; break;
        case items::ItemId::Sand: base = {190,154,87,255}; light = {233,209,151,255}; break;
        case items::ItemId::Clay: base = {133,80,58,255}; light = {186,127,91,255}; break;
        case items::ItemId::Sandstone: base = {160,112,60,255}; light = {218,176,107,255}; break;
        case items::ItemId::Snow: base = {173,204,219,255}; light = {237,246,250,255}; break;
        case items::ItemId::Ice: base = {71,142,172,255}; light = {153,218,231,255}; break;
        case items::ItemId::Wood: base = {91,57,35,255}; light = {151,105,57,255}; break;
        case items::ItemId::Leaves: base = {50,82,37,255}; light = {121,157,65,255}; break;
        case items::ItemId::GoldOre: light = {249,204,75,255}; break;
        default: break;
        }
        Fill(x + 3, y + 21, 23, 3, {8,14,17,255});
        Fill(x + 3, y + 6, 21, 16, base);
        Fill(x + 7, y + 2, 13, 22, base);
        Fill(x + 1, y + 10, 25, 8, base);
        Fill(x + 7, y + 4, 8, 3, light);
        Fill(x + 5, y + 7, 4, 4, light);
        Fill(x + 16, y + 12, 6, 5, light);
        Fill(x + 10, y + 19, 4, 2, light);
        if (item == items::ItemId::Soil) {
            Fill(x + 7, y + 1, 13, 3, {109,151,91,255});
            Fill(x + 3, y + 5, 19, 3, {81,119,70,255});
        }
    }

    void SlotBox(Rect bounds, const Slot &slot, bool selected, std::string_view key = {}) {
        const int x = int(bounds.X), y = int(bounds.Y);
        Box(x, y, 48, 48, selected ? Gold : Border, selected ? Color{49,54,38,255} : Inset);
        Fill(x + 3, y + 3, 42, 1, {47,60,53,255});
        if (slot.Item) {
            Icon(x + 11, y + 9, *slot.Item);
            const auto count = std::to_string(slot.Quantity);
            const int textX = x + 44 - int(count.size()) * 6;
            Fill(textX - 1, y + 36, int(count.size()) * 6 + 2, 9, Inset);
            Text(textX, y + 37, count, TextColor);
            if (slot.Reserved) Fill(x + 4, y + 40, 3, 3, Gold);
        } else {
            Fill(x + 22, y + 22, 4, 4, {40,53,48,255});
        }
        Text(x + 5, y + 5, key, selected ? Gold : Muted);
    }

};

std::string ItemName(const Slot &slot) {
    if (!slot.Item) return "EMPTY SLOT";
    auto name = std::string(items::Definition(*slot.Item).Name);
    std::replace(name.begin(), name.end(), '_', ' ');
    return name;
}
} // namespace

void PaintInventory(const InventoryView &view, std::vector<std::byte> &pixels) {
    Painter paint(pixels);
    paint.Box(0, 0, InventoryLayout::Width, InventoryLayout::HotbarHeight, Border, Panel);
    paint.Fill(0, 0, 3, InventoryLayout::HotbarHeight, Gold);
    paint.Text(12, 11, view.CalendarLabel.empty() ? "QUICK SLOTS" : view.CalendarLabel, TextColor);
    paint.Text(276, 11, view.Expanded ? "TAB CLOSE" : "TAB INVENTORY", Gold);
    for (size_t i = 0; i < view.QuickSlots.size(); ++i)
        paint.SlotBox(InventoryLayout::QuickSlot(i), view.QuickSlots[i], view.Selected == i && view.QuickSlots[i].Reference().has_value(), std::to_string(i + 1));
    const auto &selected = view.QuickSlots[view.Selected];
    paint.Text(12, 90, ItemName(selected), selected.Item ? TextColor : Muted);
    if (selected.Item)
        paint.Text(240, 90, "READY " + std::to_string(selected.Quantity - selected.Reserved), Muted);

    if (!view.Expanded) return;
    paint.Box(0, 120, InventoryLayout::Width, InventoryLayout::Height - 120, Border, Panel);
    paint.Text(12, 132, "INVENTORY", TextColor, 2);
    const auto capacity = std::to_string(view.Quantity) + " / " + std::to_string(view.Capacity);
    paint.Text(384 - int(capacity.size()) * 6, 136, capacity, Muted);
    paint.Fill(12, 153, 372, 1, Border);
    for (size_t i = 0; i < view.BagSlots.size(); ++i)
        paint.SlotBox(InventoryLayout::BagSlot(i), view.BagSlots[i],
            selected.Reference() && view.BagSlots[i].Reference() == selected.Reference());
    paint.Text(12, 440, "CLICK ITEM TO BIND / 1-7 SELECT", Muted);
    paint.Text(12, 453, "TAB OR ESC CLOSE", Gold);
}
} // namespace pixel::ui
