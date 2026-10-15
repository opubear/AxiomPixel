#pragma once
#include <cstdint>

namespace pixel {
// Air velocity in world pixels/second; positive Y points down.
struct WindVelocity {
    float X = 0;
    float Y = 0;
    bool operator==(const WindVelocity &) const = default;
};

// Actual velocity of a moved dirt cell at its final position. One entry per
// moved cell; occupied cells without an entry are stationary.
struct DirtMotion {
    uint32_t X, Y;
    WindVelocity Velocity;
};
} // namespace pixel
