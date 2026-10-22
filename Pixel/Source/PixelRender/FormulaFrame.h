#pragma once
#include "Gameplay/FormulaBridge/FormulaBridge.h"
#include "WorldGridFrame.h"
#include <span>

namespace pixel {
class Camera2D;
// Copies live projectile views into camera-relative, render-owned circles.
void BuildFormulaFrame(std::span<const FormulaProjectile> projectiles, const Camera2D &camera,
                       std::array<int, 2> framebufferSize, WorldGridFrame &output);
} // namespace pixel
