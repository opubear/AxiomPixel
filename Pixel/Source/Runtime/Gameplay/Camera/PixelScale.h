#pragma once
#include <array>
#include <cstdint>

// One logical pixel is one simulation cell. This setting affects only its
// projection into framebuffer pixels, never world storage or physics units.
#ifndef PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL
#define PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL 20
#endif

namespace pixel {
inline constexpr uint32_t MaximumRenderPixelsPerLogicalPixel = 64;
static_assert(PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL >= 1 &&
              PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL <= MaximumRenderPixelsPerLogicalPixel,
              "PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL must be an integer in [1, 64]");
inline constexpr uint32_t DefaultRenderPixelsPerLogicalPixel = PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL;
static_assert(DefaultRenderPixelsPerLogicalPixel == PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL,
              "PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL must be an integer");
inline constexpr std::array<float, 2> DefaultLogicalPixelSize{
    float(DefaultRenderPixelsPerLogicalPixel), float(DefaultRenderPixelsPerLogicalPixel)};
inline constexpr float MinimumSceneScale = 1.f / DefaultRenderPixelsPerLogicalPixel;
inline constexpr float MaximumSceneScale = float(MaximumRenderPixelsPerLogicalPixel) / DefaultRenderPixelsPerLogicalPixel;

constexpr bool IsValidLogicalPixelSize(std::array<float, 2> size) {
    return size[0] >= 1 && size[0] <= MaximumRenderPixelsPerLogicalPixel && size[0] == size[1];
}

constexpr bool IsValidSceneScale(float scale) {
    return scale >= MinimumSceneScale && scale <= MaximumSceneScale;
}
} // namespace pixel
