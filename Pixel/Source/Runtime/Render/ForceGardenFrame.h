#pragma once
#include "Dev/Debug/Garden/ForceGarden.h"
#include "WorldGridFrame.h"

namespace pixel {
class Camera2D;
void BuildForceGardenFrame(const std::optional<ForceGardenObservation> &garden, const Camera2D &camera,
                          std::array<int, 2> framebufferSize, WorldGridFrame &output);
} // namespace pixel
