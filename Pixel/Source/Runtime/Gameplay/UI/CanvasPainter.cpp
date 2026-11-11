#include "CanvasPainter.h"
#include <algorithm>
#include <stdexcept>

namespace pixel::ui {
namespace {
// Five columns, seven rows. UI labels intentionally use this small ASCII set;
// this avoids a font service, asset dependency or hidden ImGui draw-list use.
std::array<uint8_t, 7> Glyph(char c) {
    static constexpr std::array<std::array<uint8_t, 7>, 36> glyphs{{{14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14}, {14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30}, {2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30}, {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8}, {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}, {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16}, {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14}, {7, 2, 2, 2, 18, 18, 12}, {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31}, {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17}, {15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4}, {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4}, {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17}, {17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31}}};
    if (c >= '0' && c <= '9')
        return glyphs[c - '0'];
    if (c >= 'a' && c <= 'z')
        c = char(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return glyphs[10 + c - 'A'];
    if (c == '/')
        return {1, 1, 2, 4, 8, 16, 16};
    if (c == '-')
        return {0, 0, 0, 31, 0, 0, 0};
    if (c == ':')
        return {0, 4, 4, 0, 4, 4, 0};
    return {};
}

} // namespace

CanvasPainter::CanvasPainter(std::vector<std::byte> &pixels, int width, int height)
    : Pixels(pixels), Width(width), Height(height) {
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("canvas dimensions must be positive");
    Pixels.assign(size_t(width) * height * 4, std::byte{});
}

void CanvasPainter::Fill(int x, int y, int width, int height, Color color) {
    for (int row = std::max(0, y); row < std::min(Height, y + height); ++row)
        for (int column = std::max(0, x); column < std::min(Width, x + width); ++column) {
            const auto offset = size_t(row * Width + column) * 4;
            for (size_t c = 0; c < 4; ++c)
                Pixels[offset + c] = std::byte(color[c]);
        }
}

void CanvasPainter::Box(int x, int y, int width, int height, Color edge, Color body) {
    Fill(x, y, width, height, edge);
    Fill(x + 2, y + 2, width - 4, height - 4, body);
}

void CanvasPainter::Text(int x, int y, std::string_view value, Color color, int scale) {
    for (char c : value) {
        const auto glyph = Glyph(c);
        for (int row = 0; row < 7; ++row)
            for (int column = 0; column < 5; ++column)
                if (glyph[row] & (1 << (4 - column)))
                    Fill(x + column * scale, y + row * scale, scale, scale, color);
        x += 6 * scale;
    }
}

} // namespace pixel::ui
