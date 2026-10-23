#pragma once
#include <compare>
#include <cstdint>

namespace pixel {
// A small ecology rule driven by measured horizontal acceleration, in cell/s².
// The two beds retain accumulated healthy time; absence never simulates catch-up.
inline constexpr uint32_t GardenMatureSteps = 180;
inline constexpr float GardenMinimumForce = 1800, GardenMaximumForce = 5400;
inline constexpr int64_t GardenCrystalX = 480, GardenWildX = 484, GardenHomeX = 540;
inline constexpr int64_t GardenDeviceX = 530;
inline constexpr uint64_t GardenCrystalSubject = 0x47415244454e;

struct ForceGardenState {
    bool Enabled = false, CrystalEnabled = true;
    int64_t SurfaceY = 512;
    uint32_t WildGrowth = 0, HomeGrowth = 0;
    auto operator<=>(const ForceGardenState &) const = default;
};
enum class GardenCondition { OutsideActiveArea, Obstructed, NeedsForce, ExcessForce, Growing, Mature };
struct GardenSample {
    GardenCondition Condition = GardenCondition::OutsideActiveArea;
    float HorizontalAcceleration = 0;
    bool Sampled = false;
};
struct ForceGardenObservation {
    ForceGardenState State;
    GardenSample Wild, Home;
};
} // namespace pixel
