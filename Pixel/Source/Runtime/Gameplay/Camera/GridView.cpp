#include "GridView.h"
#include <cmath>
#include <stdexcept>

std::optional<std::array<int64_t, 2>> PickGridCell(const GridView2D &view,
                                                   std::array<double, 2> cursor,
                                                   std::array<int, 2> windowSize,
                                                   std::array<int, 2> framebufferSize) {
    if (!pixel::IsValidLogicalPixelSize(view.CellSizePixels))
        return std::nullopt;
    std::array<int64_t, 2> cell{};
    for (size_t axis = 0; axis < 2; ++axis) {
        if (windowSize[axis] <= 0 || framebufferSize[axis] <= 0 || !std::isfinite(cursor[axis]) || cursor[axis] < 0 || cursor[axis] >= windowSize[axis])
            return std::nullopt;
        const double pixel = std::floor(cursor[axis] * framebufferSize[axis] / windowSize[axis]) + view.OriginPixels[axis];
        const double local = std::floor(pixel / view.CellSizePixels[axis]);
        if (local < double(INT64_MIN) || local >= double(INT64_MAX))
            return std::nullopt;
        const int64_t offset = int64_t(local);
        if ((offset > 0 && view.WorldOriginCells[axis] > INT64_MAX - offset) || (offset < 0 && view.WorldOriginCells[axis] < INT64_MIN - offset))
            return std::nullopt;
        cell[axis] = view.WorldOriginCells[axis] + offset;
    }
    return cell;
}

GridView2D MakeCameraView(const pixel::Camera2D &camera, std::array<int, 2> framebufferSize,
                          GridView2D settings) {
    if (!pixel::IsValidLogicalPixelSize(settings.CellSizePixels))
        throw std::invalid_argument("logical pixel must project to a square of 1 to 64 framebuffer pixels per side");
    const auto cameraPosition = camera.GetWorldPosition();
    const pixel::CellCoord reference{cameraPosition.Chunk.X * pixel::storage::ChunkEdge,
                                     cameraPosition.Chunk.Y * pixel::storage::ChunkEdge};
    const auto local = cameraPosition.RelativeTo(reference);
    settings.WorldOriginCells = {reference.X, reference.Y};
    for (size_t axis = 0; axis < 2; ++axis) {
        if (framebufferSize[axis] < 0)
            throw std::invalid_argument("camera projection requires a nonnegative viewport");
        const double origin = std::round(local[axis] * settings.CellSizePixels[axis] - framebufferSize[axis] / 2.0);
        if (origin < INT32_MIN || origin > INT32_MAX)
            throw std::invalid_argument("camera viewport exceeds local pixel coordinates");
        settings.OriginPixels[axis] = int32_t(origin);
    }
    return settings;
}
