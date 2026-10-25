#pragma once

#include "ForceField.h"
#include "Runtime/Simulation/World/Environment/Wind/WindTypes.h"
#include "Runtime/Simulation/World/CellState.h"

namespace pixel::FieldResponse {
// Legacy gameplay contract: one neighbouring-cell move per fixed step, without
// particle inertia. Keep unit conversions here rather than in the air solver.
inline constexpr double StepSeconds = 1.0 / 60.0;
inline constexpr float StepsPerSecond = 60;
inline constexpr float DirectionAcceleration = StepsPerSecond * StepsPerSecond;
inline constexpr float MovementDeadZone = StepsPerSecond / 2;

inline ForceVector FromDirection(ForceDirection direction) {
    return {direction.X * DirectionAcceleration, direction.Y * DirectionAcceleration};
}
inline ForceVector AddWind(ForceVector base, WindVelocity air) {
    return {base.X + StepsPerSecond * air.X, base.Y + StepsPerSecond * air.Y};
}
inline WindVelocity MovementVelocity(ForceVector force) {
    return {force.X / StepsPerSecond, force.Y / StepsPerSecond};
}
inline int MovementComponent(float speed) {
    if (speed > MovementDeadZone)
        return 1;
    if (speed < -MovementDeadZone)
        return -1;
    return 0;
}
inline ForceDirection MovementDirection(WindVelocity velocity) {
    return {MovementComponent(velocity.X), MovementComponent(velocity.Y)};
}
} // namespace pixel::FieldResponse
