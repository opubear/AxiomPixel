#include "InventoryPainter.h"
#include <algorithm>
#include <string>
#include <string_view>

namespace pixel::ui {
namespace {
using Color = std::array<uint8_t, 4>;
constexpr Color Border{75, 92, 83, 255}, Panel{24, 35, 33, 255}, Inset{16, 24, 25, 255};
constexpr Color TextColor{222, 225, 198, 255}, Muted{139, 160, 147, 255}, Gold{231, 189, 101, 255};

// Five columns, seven rows. UI labels intentionally use this small ASCII set;
// this avoids a font service, asset dependency or hidden ImGui draw-list use.
std::array<uint8_t, 7> Glyph(char c) {
    static constexpr std::array<std::array<uint8_t, 7>, 36> glyphs{{
        {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,2,4,8,31},
        {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
        {14,17,17,15,1,1,14},
        {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
        {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
        {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
        {7,2,2,2,18,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
        {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
        {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}}};
    if (c >= '0' && c <= '9') return glyphs[c - '0'];
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return glyphs[10 + c - 'A'];
    if (c == '/') return {1,1,2,4,8,16,16};
    if (c == '-') return {0,0,0,31,0,0,0};
    if (c == ':') return {0,4,4,0,4,4,0};
    return {};
}

class Painter {
  public:
    explicit Painter(std::vector<std::byte> &pixels) : Pixels(pixels) {
        Pixels.assign(size_t(InventoryLayout::Width) * InventoryLayout::Height * 4, std::byte{});
    }

    void Fill(int x, int y, int width, int height, Color color) {
        for (int row = std::max(0, y); row < std::min(InventoryLayout::Height, y + height); ++row)
            for (int column = std::max(0, x); column < std::min(InventoryLayout::Width, x + width); ++column) {
                const auto offset = size_t(row * InventoryLayout::Width + column) * 4;
                for (size_t c = 0; c < 4; ++c) Pixels[offset + c] = std::byte(color[c]);
            }
    }

    void Box(int x, int y, int width, int height, Color edge, Color body) {
        Fill(x, y, width, height, edge);
        Fill(x + 2, y + 2, width - 4, height - 4, body);
    }

    void Text(int x, int y, std::string_view value, Color color, int scale = 1) {
        for (char c : value) {
            const auto glyph = Glyph(c);
            for (int row = 0; row < 7; ++row)
                for (int column = 0; column < 5; ++column)
                    if (glyph[row] & (1 << (4 - column)))
                        Fill(x + column * scale, y + row * scale, scale, scale, color);
            x += 6 * scale;
        }
    }

    void Icon(int x, int y, items::ItemId item) {
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

  private:
    std::vector<std::byte> &Pixels;
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
    paint.Text(12, 11, "QUICK SLOTS", TextColor);
    paint.Text(276, 11, view.Expanded ? "TAB CLOSE" : "TAB INVENTORY", Gold);
    for (size_t i = 0; i < view.QuickSlots.size(); ++i)
        paint.SlotBox(InventoryLayout::QuickSlot(i), view.QuickSlots[i], view.Selected == i, std::to_string(i + 1));
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
            view.BagSlots[i].Item && view.BagSlots[i].Item == selected.Item);
    paint.Text(12, 440, "CLICK ITEM TO BIND / 1-7 SELECT", Muted);
    paint.Text(12, 453, "TAB OR ESC CLOSE", Gold);
}
} // namespace pixel::ui
