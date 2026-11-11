#pragma once
#include "Runtime/Render/Assets/ArtAssets.h"
#include "WorldGridFrame.h"

namespace pixel {
float BackgroundPhase(const BackgroundLayer &layer, const GridView2D &view, uint32_t framebufferWidth);
// Small, bounded phases keep remote camera coordinates out of the GPU.
std::array<float, 4> BackgroundPhases(const BackgroundAssets &assets, const GridView2D &view,
                                    uint32_t framebufferWidth);
} // namespace pixel
