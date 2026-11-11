#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace pixel::ui {
using Color = std::array<uint8_t, 4>;
namespace palette {
inline constexpr Color Border{75, 92, 83, 255}, Panel{24, 35, 33, 255}, Inset{16, 24, 25, 255};
inline constexpr Color TextColor{222, 225, 198, 255}, Muted{139, 160, 147, 255}, Gold{231, 189, 101, 255};
} // namespace palette

// Shared native rectangles and bitmap text. No windowing, ImGui or GPU state.
class CanvasPainter {
  public:
    CanvasPainter(std::vector<std::byte> &pixels, int width, int height);
    void Fill(int x, int y, int width, int height, Color color);
    void Box(int x, int y, int width, int height, Color edge, Color body);
    void Text(int x, int y, std::string_view value, Color color, int scale = 1);

  private:
    std::vector<std::byte> &Pixels;
    int Width, Height;
};
} // namespace pixel::ui
