#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace pixel::memory {
// Integer origins retain far-world precision. Spacing is independent of memory
// tiles and may be fractional (e.g. a MAC grid clipped to a moving domain).
struct GridGeometry {
    int64_t OriginX = 0, OriginY = 0;
    double SpacingX = 1, SpacingY = 1;
    bool operator==(const GridGeometry &) const = default;

    void Validate() const {
        if (!std::isfinite(SpacingX) || !std::isfinite(SpacingY) || SpacingX <= 0 || SpacingY <= 0)
            throw std::invalid_argument("grid sample spacing must be finite and positive");
    }
    std::array<int64_t, 2> SampleCell(int64_t x, int64_t y) const {
        Validate();
        // Subtract in integers before converting: neighbouring far coordinates
        // must not collapse to the same floating-point position.
        const auto offset = [](int64_t value, int64_t origin, double spacing) {
            const bool negative = value < origin;
            const uint64_t magnitude = negative ? uint64_t(origin) - uint64_t(value) : uint64_t(value) - uint64_t(origin);
            const long double sample = std::floor((negative ? -static_cast<long double>(magnitude) : static_cast<long double>(magnitude)) / spacing);
            if (sample < INT64_MIN || sample >= -static_cast<long double>(INT64_MIN))
                throw std::out_of_range("grid sample coordinate overflow");
            return int64_t(sample);
        };
        return {offset(x, OriginX, SpacingX), offset(y, OriginY, SpacingY)};
    }
};
} // namespace pixel::memory
