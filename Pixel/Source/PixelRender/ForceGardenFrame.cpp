#include "ForceGardenFrame.h"
#include "Camera/Camera2D.h"
#include <algorithm>

namespace pixel {
void BuildForceGardenFrame(const std::optional<ForceGardenObservation> &garden, const Camera2D &camera,
                          std::array<int, 2> framebufferSize, WorldGridFrame &output) {
    output.GardenDraws.clear();
    if (!garden || !garden->State.Enabled || framebufferSize[0] <= 0 || framebufferSize[1] <= 0)
        return;

    const auto reference = camera.GetWorldPosition().Cell();
    const auto centre = camera.RelativeTo(reference);
    const auto relativeCell = [](int64_t cell, int64_t origin) {
        return cell >= origin ? double(uint64_t(cell) - uint64_t(origin))
                              : -double(uint64_t(origin) - uint64_t(cell));
    };
    const auto shape = [&](GardenObject object, int64_t x, double dx, double dy, double rx, double ry,
                           std::array<float, 4> color, bool ellipse = false) {
        const double px = (relativeCell(x, reference.X) + dx - centre[0]) * output.View.CellSizePixels[0] + framebufferSize[0] / 2.0;
        const double py = (relativeCell(garden->State.SurfaceY, reference.Y) + dy - centre[1]) * output.View.CellSizePixels[1] + framebufferSize[1] / 2.0;
        rx *= output.View.CellSizePixels[0];
        ry *= output.View.CellSizePixels[1];
        if (px + rx < 0 || py + ry < 0 || px - rx > framebufferSize[0] || py - ry > framebufferSize[1])
            return;
        output.GardenDraws.push_back({{float(px), float(py), float(rx), float(ry)}, color, object, ellipse});
    };

    const auto crystalColor = garden->State.CrystalEnabled ? std::array<float, 4>{.10f, .72f, .86f, 1}
                                                         : std::array<float, 4>{.24f, .29f, .34f, 1};
    shape(GardenObject::Crystal, GardenCrystalX, 0, -4, .7, 1.5, crystalColor);
    shape(GardenObject::Crystal, GardenCrystalX, 0, -4, 1.15, .65, crystalColor);
    shape(GardenObject::Crystal, GardenCrystalX, -.15, -4.45, .2, .65, {.65f, .95f, 1, 1});
    const auto plant = [&](GardenObject object, int64_t x, uint32_t growth) {
        const bool mature = growth >= GardenMatureSteps;
        const double height = 1.2 + 1.4 * std::min(double(growth) / GardenMatureSteps, 1.0);
        shape(object, x, .5, -height * .5, .12, height * .5, {.12f, .48f, .14f, 1});
        shape(object, x, .14, -height * .55, .42, .2, {.24f, .78f, .22f, 1}, true);
        shape(object, x, .86, -height * .75, .42, .2, {.30f, .86f, .26f, 1}, true);
        if (mature)
            shape(object, x, .5, -height, .55, .48, {1, .68f, .14f, 1}, true);
    };
    plant(GardenObject::WildPlant, GardenWildX, garden->State.WildGrowth);
    plant(GardenObject::HomePlant, GardenHomeX, garden->State.HomeGrowth);
}
} // namespace pixel
