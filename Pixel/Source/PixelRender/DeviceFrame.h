#pragma once
#include "Devices/DeviceTypes.h"
#include "WorldGridFrame.h"
#include <span>

namespace pixel {
class Camera2D;
// Converts a committed device snapshot into independent framebuffer geometry.
void BuildDeviceFrame(std::span<const DeviceSnapshot> devices, const Camera2D &camera,
                      std::array<int, 2> framebufferSize, WorldGridFrame &output);
} // namespace pixel
