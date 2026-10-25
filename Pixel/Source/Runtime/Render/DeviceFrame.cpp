#include "DeviceFrame.h"
#include "Runtime/Gameplay/Camera/Camera2D.h"
#include <stdexcept>

namespace pixel {
namespace {
std::array<float, 4> DeviceColor(DeviceState state) {
    switch (state) {
    case DeviceState::Stopped: return {.22f, .25f, .29f, 1};
    case DeviceState::Running: return {.14f, .68f, .42f, 1};
    case DeviceState::NeedsFuel: return {.72f, .29f, .12f, 1};
    case DeviceState::WaitingForEnvironment: return {.80f, .60f, .12f, 1};
    case DeviceState::OutsideActiveArea: return {.32f, .34f, .52f, 1};
    case DeviceState::WaitingForCapacity: return {.63f, .24f, .62f, 1};
    }
    throw std::logic_error("unknown device presentation state");
}
} // namespace
void BuildDeviceFrame(std::span<const DeviceSnapshot> devices, const Camera2D &camera,
                      std::array<int, 2> framebufferSize, WorldGridFrame &output) {
    output.DeviceDraws.clear();
    if (framebufferSize[0] <= 0 || framebufferSize[1] <= 0)
        return;

    const auto reference = camera.GetWorldPosition().Cell();
    const auto centre = camera.RelativeTo(reference);
    const double radiusX = DeviceWidth * .5 * output.View.CellSizePixels[0];
    const double radiusY = DeviceHeight * .5 * output.View.CellSizePixels[1];
    const auto relativeCell = [](int64_t cell, int64_t origin) {
        return cell >= origin ? double(uint64_t(cell) - uint64_t(origin))
                              : -double(uint64_t(origin) - uint64_t(cell));
    };
    for (const auto &device : devices) {
        // Unsigned differences preserve nearby cells at distant coordinates and
        // also support culling devices at the opposite signed-coordinate limit.
        const double x = (relativeCell(device.X, reference.X) - centre[0]) * output.View.CellSizePixels[0] + framebufferSize[0] / 2.0 + radiusX;
        const double y = (relativeCell(device.Y, reference.Y) - centre[1]) * output.View.CellSizePixels[1] + framebufferSize[1] / 2.0 + radiusY;
        if (x + radiusX < 0 || y + radiusY < 0 || x - radiusX > framebufferSize[0] || y - radiusY > framebufferSize[1])
            continue;

        DeviceDraw draw;
        draw.CenterRadius = {float(x), float(y), float(radiusX), float(radiusY)};
        draw.Color = DeviceColor(device.State);
        draw.OutputCenterRadius = {float(x + radiusX * .75), float(y), float(radiusX * .25), float(radiusY * .4)};
        draw.OutputColor = device.State == DeviceState::Running ? std::array<float, 4>{.65f, 1, 1, 1}
                                                              : std::array<float, 4>{.75f, .75f, .72f, 1};
        draw.DeviceId = device.Ref.Id;
        output.DeviceDraws.push_back(draw);
    }
}
} // namespace pixel
