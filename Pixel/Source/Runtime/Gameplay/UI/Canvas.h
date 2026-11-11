#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pixel::ui {
struct Rect {
    float X = 0, Y = 0, Width = 0, Height = 0;
    bool Contains(float x, float y) const {
        return x >= X && y >= Y && x < X + Width && y < Y + Height;
    }
};

// Owned pixels. Visibility and scale are captured per render frame slot;
// Revision changes only when the pixel content changes.
struct Canvas {
    uint64_t Revision = 0;
    std::vector<std::byte> Pixels;
    float UIScale = 1;
    bool Visible = true;
};
} // namespace pixel::ui
