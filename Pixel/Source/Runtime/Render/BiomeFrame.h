#pragma once
#include "Runtime/Gameplay/CommittedWorldView.h"
#include "Runtime/Gameplay/Camera/Camera2D.h"
#include "WorldGridFrame.h"

namespace pixel {
inline constexpr size_t MaximumBiomeDecorationParts = 256;
void BuildBiomeFrame(const CommittedWorldView &world, const Camera2D &camera,
                     std::array<int, 2> framebufferSize, WorldGridFrame &output);
} // namespace pixel
