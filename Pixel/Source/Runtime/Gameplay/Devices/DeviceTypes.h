#pragma once
#include <cstdint>

namespace pixel {
struct DeviceRef {
    uint64_t Id = 0, Version = 0;
    bool operator==(const DeviceRef &) const = default;
};
enum class DeviceState { Stopped, Running, NeedsFuel, WaitingForEnvironment, OutsideActiveArea, WaitingForCapacity };
const char *DeviceStateName(DeviceState state);
// One native type in this slice; content identity is independent of render assets.
inline constexpr uint32_t ForceEmitterType = 3001, DeviceWidth = 2, DeviceHeight = 2;
struct DeviceSnapshot {
    DeviceRef Ref;
    uint64_t Owner = 0;
    int64_t X = 0, Y = 0;
    bool Enabled = false;
    DeviceState State = DeviceState::Stopped;
    uint64_t Instance = 0, Cycles = 0;
    uint32_t Fuel = 0;
    bool operator==(const DeviceSnapshot &) const = default;
};
} // namespace pixel
