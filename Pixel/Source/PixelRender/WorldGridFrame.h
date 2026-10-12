#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Grid2D {
inline constexpr uint32_t Width = 1024;
inline constexpr uint32_t Height = 1024;
inline constexpr uint32_t PageHeight = 256;
inline constexpr uint32_t PageCount = Height / PageHeight;
inline constexpr size_t CellCount = size_t(Width) * Height;
inline constexpr size_t UploadBytes = CellCount * sizeof(uint32_t);
inline constexpr size_t PageBytes = UploadBytes / PageCount;
} // namespace Grid2D
struct GridView2D {
    std::array<int32_t, 2> OriginPixels{0, 0};
    std::array<uint32_t, 2> CellSizePixels{16, 16};
    std::array<float, 4> Background{0, 0, 0, 1};
};
// Cursor coordinates use window units; rendering uses framebuffer pixels.
std::optional<std::array<uint32_t, 2>> PickGridCell(const GridView2D &view,
                                                    std::array<double, 2> cursor,
                                                    std::array<int, 2> windowSize,
                                                    std::array<int, 2> framebufferSize);
struct PostProcessParameters {
    std::array<float, 16> Values{1, 1, 1, 1};
};
enum class GridDisplayMode : uint32_t {
    Materials = 0,
    ForceField = 1,
};
// Render-owned character snapshot. Bounds use continuous framebuffer pixels.
struct CharacterDraw {
    std::array<float, 4> Bounds{}; // left, top, width, height
    std::array<float, 4> HeadColor{}; // linear RGBA
    std::array<float, 4> BodyColor{};
    std::array<float, 4> Parameters{0.25f, 0, 0, 0}; // head height fraction
};
static_assert(sizeof(CharacterDraw) == 64);
struct WorldGridFrameView {
    // Material layer IDs, or packed linear RGB8 (R in bits 0..7) in ForceField mode.
    std::span<const uint32_t> Cells;
    GridView2D View;
    PostProcessParameters Post;
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;
    std::optional<CharacterDraw> Character;
};
// Persistent render-owned output, filled at FrameBegin. Gameplay storage is
// converted into this format; it is never reinterpreted as texture indices.
class WorldGridFrame {
  public:
    WorldGridFrame() : CellStorage(Grid2D::CellCount) {}
    WorldGridFrame(const WorldGridFrame &) = delete;
    WorldGridFrame &operator=(const WorldGridFrame &) = delete;
    std::span<uint32_t> Cells() { return CellStorage; }
    WorldGridFrameView Read() const { return {CellStorage, View, Post, DisplayMode, Character}; }
    GridView2D View;
    PostProcessParameters Post;
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;
    std::optional<CharacterDraw> Character;

  private:
    std::vector<uint32_t> CellStorage;
};
// Layer zero is reserved for empty cells. Pixels contain tightly packed RGBA8,
// in layer-major order; all layers share the same dimensions and sRGB encoding.
struct GridTextureCatalog {
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Layers = 0;
    std::vector<std::byte> Pixels;
};
void ValidateGridTextureCatalog(const GridTextureCatalog &catalog);
void ValidateWorldGridFrame(const WorldGridFrameView &frame, uint32_t textureLayers,
                            uint32_t framebufferWidth, uint32_t framebufferHeight);
