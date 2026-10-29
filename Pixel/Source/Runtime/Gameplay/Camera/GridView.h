#pragma once
#include "Camera2D.h"
#include "PixelScale.h"
#include <array>
#include <cstdint>
#include <optional>

struct GridView2D {
    std::array<int32_t, 2> OriginPixels{0, 0};
    // Framebuffer pixels per logical world pixel. Both axes must be equal;
    // this is independent of window DPI and source artwork texel density.
    std::array<uint32_t, 2> CellSizePixels = pixel::DefaultLogicalPixelSize;
    std::array<float, 4> Background{0, 0, 0, 1};
    // OriginPixels is relative to this world-cell reference. GPU coordinates
    // remain small even when the camera is far from the world origin.
    std::array<int64_t, 2> WorldOriginCells{};
};
// Cursor coordinates use window units; rendering uses framebuffer pixels.
std::optional<std::array<int64_t, 2>> PickGridCell(const GridView2D &view,
                                                   std::array<double, 2> cursor,
                                                   std::array<int, 2> windowSize,
                                                   std::array<int, 2> framebufferSize);
// Camera position is authoritative; this value describes its pixel projection.
GridView2D MakeCameraView(const pixel::Camera2D &camera, std::array<int, 2> framebufferSize,
                          GridView2D settings);
