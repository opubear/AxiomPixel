#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace pixel {
// Open boundaries admit the ambient flow and let disturbances leave the domain.
// Periodic is an explicit numerical-validation setting, never a game default.
enum class WindBoundaryMode { Open,
                              Periodic };
enum class WindBoundarySide { Left,
                              Right,
                              Top,
                              Bottom };

inline bool IsValidWindBoundary(WindBoundaryMode mode) {
    return mode == WindBoundaryMode::Open || mode == WindBoundaryMode::Periodic;
}

inline double WindDistance(double difference, double extent, WindBoundaryMode mode) {
    return mode == WindBoundaryMode::Periodic ? std::remainder(difference, extent) : difference;
}

// Subtract integer world coordinates before converting to a solver coordinate.
// Saturation also treats an unsupported, overflowing difference as outside any
// finite wind window, without relying on platform-specific long-double precision.
inline int64_t WindCoordinateOffset(int64_t coordinate, int64_t origin) {
    if (origin > 0 && coordinate < std::numeric_limits<int64_t>::min() + origin)
        return std::numeric_limits<int64_t>::min();
    if (origin < 0 && coordinate > std::numeric_limits<int64_t>::max() + origin)
        return std::numeric_limits<int64_t>::max();
    return coordinate - origin;
}

// Air velocity in world pixels/second; positive Y points down.
struct alignas(8) WindVelocity {
    float X = 0;
    float Y = 0;
    bool operator==(const WindVelocity &) const = default;
};

inline bool IsWindInflow(WindBoundarySide side, WindVelocity ambient) {
    switch (side) {
    case WindBoundarySide::Left:
        return ambient.X >= 0;
    case WindBoundarySide::Right:
        return ambient.X < 0;
    case WindBoundarySide::Top:
        return ambient.Y >= 0;
    case WindBoundarySide::Bottom:
        return ambient.Y < 0;
    }
    return false;
}

// Actual velocity of a moved dirt cell at its final position. One entry per
// moved cell; occupied cells without an entry are stationary.
struct DirtMotion {
    uint32_t X, Y;
    WindVelocity Velocity;
};
} // namespace pixel
