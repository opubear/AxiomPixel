#pragma once
#include "Runtime/Simulation/World/WorldCoordinates.h"

namespace pixel {
struct HealthState {
    float Current = 100, Maximum = 100;
    uint32_t InvulnerableTicks = 0;
    WorldPosition Respawn;
};
} // namespace pixel
