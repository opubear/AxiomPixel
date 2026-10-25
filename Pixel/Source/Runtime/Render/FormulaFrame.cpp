#include "FormulaFrame.h"
#include "Runtime/Gameplay/Camera/Camera2D.h"
#include <cmath>

namespace pixel {
void BuildFormulaFrame(std::span<const FormulaProjectile> projectiles, const Camera2D &camera,
                       std::array<int, 2> framebufferSize, WorldGridFrame &output) {
    output.FormulaDraws.clear();
    if (framebufferSize[0] <= 0 || framebufferSize[1] <= 0)
        return;

    const auto &cameraPosition = camera.GetWorldPosition();
    const auto reference = cameraPosition.Cell();
    const auto centre = cameraPosition.RelativeTo(reference);
    for (const auto &projectile : projectiles) {
        const auto local = projectile.Position.RelativeTo(reference);
        const double x = (local[0] - centre[0]) * output.View.CellSizePixels[0] + framebufferSize[0] / 2.0;
        const double y = (local[1] - centre[1]) * output.View.CellSizePixels[1] + framebufferSize[1] / 2.0;
        const double radiusX = projectile.Radius * output.View.CellSizePixels[0];
        const double radiusY = projectile.Radius * output.View.CellSizePixels[1];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radiusX) || !std::isfinite(radiusY) || radiusX <= 0 || radiusY <= 0)
            continue;
        if (x + radiusX < 0 || y + radiusY < 0 || x - radiusX > framebufferSize[0] || y - radiusY > framebufferSize[1])
            continue;

        FormulaDraw draw;
        draw.CenterRadius = {float(x), float(y), float(radiusX), float(radiusY)};
        draw.WorldEpoch = projectile.WorldEpoch;
        draw.CastId = projectile.CastId;
        draw.ProjectileId = projectile.ProjectileId;
        output.FormulaDraws.push_back(draw);
    }
}
} // namespace pixel
