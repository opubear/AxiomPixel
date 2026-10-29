#pragma once
#include <array>
#include <cstdint>

// One logical pixel is one simulation cell. This setting affects only its
// projection into framebuffer pixels, never world storage or physics units.
#ifndef PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL
#define PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL 4
#endif

namespace pixel {
inline constexpr uint32_t MaximumRenderPixelsPerLogicalPixel = 64;
static_assert(PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL >= 1 &&
              PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL <= MaximumRenderPixelsPerLogicalPixel,
              "PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL must be an integer in [1, 64]");
inline constexpr uint32_t DefaultRenderPixelsPerLogicalPixel = PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL;
static_assert(DefaultRenderPixelsPerLogicalPixel == PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL,
              "PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL must be an integer");
inline constexpr std::array<uint32_t, 2> DefaultLogicalPixelSize{
    DefaultRenderPixelsPerLogicalPixel, DefaultRenderPixelsPerLogicalPixel};

constexpr bool IsValidLogicalPixelSize(std::array<uint32_t, 2> size) {
    return size[0] >= 1 && size[0] <= MaximumRenderPixelsPerLogicalPixel && size[0] == size[1];
}
} // namespace pixel
