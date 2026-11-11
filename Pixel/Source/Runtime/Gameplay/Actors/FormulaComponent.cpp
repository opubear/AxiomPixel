#include "FormulaComponent.h"
#include "Actor.h"
#include "BodyComponent.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace pixel {
FormulaComponent::FormulaComponent(Actor &owner, api::ProgramRef program) : Component(owner) {
    Equip(program);
}

void FormulaComponent::Equip(api::ProgramRef program) {
    if (!program.ResourceId || !program.Version)
        throw std::invalid_argument("formula component requires a valid program");
    Program = program;
}

FormulaLaunch FormulaComponent::Aim(WorldPosition target, float projectileRadius,
                                    std::array<double, 2> fallbackDirection) const {
    return AimFromBody(GetOwner().GetComponent<BodyComponent>()->GetBody(), target,
                       projectileRadius, fallbackDirection);
}

FormulaLaunch FormulaComponent::AimFromBody(const Body &body, WorldPosition target, float projectileRadius,
                                           std::array<double, 2> fallbackDirection) {
    auto center = body.GetWorldPosition();
    center.Local[0] += body.Size[0] / 2;
    center.Local[1] += body.Size[1] / 2;
    center.Normalize();
    target.Normalize();
    const auto reference = center.Cell();
    const auto mouse = target.RelativeTo(reference);
    const auto player = center.RelativeTo(reference);
    const auto directionTo = [&](const std::array<float, 2> &from, std::array<double, 2> fallback) {
        const double dx = double(mouse[0]) - from[0], dy = double(mouse[1]) - from[1];
        const double length = std::hypot(dx, dy);
        return length > 1e-6 ? std::array<double, 2>{dx / length, dy / length} : fallback;
    };
    const auto outward = directionTo(player, fallbackDirection);
    const double horizontal = outward[0] == 0 ? std::numeric_limits<double>::infinity()
        : (body.Size[0] / 2 + projectileRadius) / std::abs(outward[0]);
    const double vertical = outward[1] == 0 ? std::numeric_limits<double>::infinity()
        : (body.Size[1] / 2 + projectileRadius) / std::abs(outward[1]);
    const double distance = std::min(horizontal, vertical) + LaunchClearance;
    auto origin = center;
    origin.Local[0] += float(outward[0] * distance);
    origin.Local[1] += float(outward[1] * distance);
    origin.Normalize();

    // The cursor may lie between the body and muzzle. Always aim from the
    // actual muzzle, including that case; a coincident cursor keeps outward aim.
    const auto direction = directionTo(origin.RelativeTo(reference), outward);
    return {center, origin, direction, std::atan2(direction[1], direction[0]) * 180 / std::numbers::pi};
}
} // namespace pixel
