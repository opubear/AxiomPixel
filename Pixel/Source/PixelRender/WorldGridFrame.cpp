#include "WorldGridFrame.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

std::optional<std::array<uint32_t, 2>> PickGridCell(const GridView2D &view,
                                                    std::array<double, 2> cursor,
                                                    std::array<int, 2> windowSize,
                                                    std::array<int, 2> framebufferSize) {
    std::array<uint32_t, 2> cell{};
    constexpr std::array worldSize{Grid2D::Width, Grid2D::Height};
    for (size_t axis = 0; axis < 2; ++axis) {
        if (windowSize[axis] <= 0 || framebufferSize[axis] <= 0 || !view.CellSizePixels[axis] || !std::isfinite(cursor[axis]) || cursor[axis] < 0 || cursor[axis] >= windowSize[axis])
            return std::nullopt;
        const double pixel = std::floor(cursor[axis] * framebufferSize[axis] / windowSize[axis]) + view.OriginPixels[axis];
        if (pixel < 0 || pixel >= double(worldSize[axis]) * view.CellSizePixels[axis])
            return std::nullopt;
        cell[axis] = uint32_t(pixel / view.CellSizePixels[axis]);
    }
    return cell;
}

void ValidateGridTextureCatalog(const GridTextureCatalog &catalog) {
    const auto maximum = std::numeric_limits<size_t>::max();
    if (!catalog.Width || !catalog.Height || !catalog.Layers || size_t(catalog.Width) > maximum / 4 / catalog.Height / catalog.Layers || catalog.Pixels.size() != size_t(catalog.Width) * catalog.Height * catalog.Layers * 4)
        throw std::invalid_argument("texture catalog requires equal-sized, tightly packed RGBA8 layers");
}
void ValidateWorldGridFrame(const WorldGridFrameView &frame, uint32_t layers, uint32_t width, uint32_t height) {
    if (frame.Cells.size() != Grid2D::CellCount || !layers || !width || !height)
        throw std::invalid_argument("grid frame requires 1024x1024 cells and a drawable viewport");
    constexpr auto maximum = std::numeric_limits<int32_t>::max();
    const std::array<uint32_t, 2> world{Grid2D::Width, Grid2D::Height};
    const std::array<uint32_t, 2> extent{width, height};
    for (size_t axis = 0; axis < 2; ++axis) {
        const auto size = frame.View.CellSizePixels[axis];
        if (!size || uint64_t(size) * world[axis] > maximum || extent[axis] > uint32_t(maximum) || int64_t(frame.View.OriginPixels[axis]) + extent[axis] - 1 > maximum)
            throw std::invalid_argument("grid view dimensions or origin exceed integer pixel coordinates");
    }
    if (frame.View.Background[3] != 1 || std::any_of(frame.View.Background.begin(), frame.View.Background.end(), [](float v) { return !std::isfinite(v) || v < 0 || v > 1; }) || std::any_of(frame.Post.Values.begin(), frame.Post.Values.end(), [](float v) { return !std::isfinite(v); }))
        throw std::invalid_argument("grid background must be opaque and rendering parameters finite");
    switch (frame.DisplayMode) {
    case GridDisplayMode::Materials:
        if (std::any_of(frame.Cells.begin(), frame.Cells.end(), [layers](uint32_t id) { return id >= layers; }))
            throw std::invalid_argument("grid TextureId exceeds the texture catalog");
        break;
    case GridDisplayMode::ForceField:
        if (std::any_of(frame.Cells.begin(), frame.Cells.end(), [](uint32_t color) { return color > 0xFFFFFF; }))
            throw std::invalid_argument("force-field color must be packed RGB8");
        break;
    default:
        throw std::invalid_argument("unknown grid display mode");
    }
}
