#pragma once
#include "Assets/ArtAssets.h"
#include "WorldGridFrame.h"

namespace pixel {
// Small, bounded phases keep remote camera coordinates out of the GPU.
std::array<float, 4> BackgroundPhases(const BackgroundAssets &assets, const GridView2D &view,
                                    uint32_t framebufferWidth);
} // namespace pixel
