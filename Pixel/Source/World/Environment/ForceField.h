#pragma once

#include "Performance/MemoryUsage.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace pixel {
// Acceleration in world pixels/s^2 (unit mass); positive Y is down.
// This type and storage have no dependency on air velocity or a wind solver.
struct ForceVector {
    float X = 0, Y = 0;
    bool operator==(const ForceVector &) const = default;
};

// One vector per world pixel. Sources add to the same spatial field; material
// motion never transports it. Callers decide when to clear transient sources.
class ForceField {
  public:
    performance::MemoryUsage GetMemoryUsage() const {
        return performance::MeasureObject(*this, Values);
    }

    ForceField(uint32_t width, uint32_t height) : Width(width), Height(height) {
        const uint64_t count = uint64_t(width) * height;
        if (!width || !height || count > Values.max_size())
            throw std::invalid_argument("force field dimensions must fit a nonempty array");
        Values.resize(size_t(count));
    }
    uint32_t GetWidth() const { return Width; }
    uint32_t GetHeight() const { return Height; }
    ForceVector Get(uint32_t x, uint32_t y) const { return Values.at(Index(x, y)); }
    void Set(uint32_t x, uint32_t y, ForceVector value) {
        Validate(value);
        Values.at(Index(x, y)) = value;
    }
    void Add(uint32_t x, uint32_t y, ForceVector value) {
        const auto old = Get(x, y);
        Set(x, y, {old.X + value.X, old.Y + value.Y});
    }
    void Fill(ForceVector value) {
        Validate(value);
        std::fill(Values.begin(), Values.end(), value);
    }

  private:
    uint32_t Width, Height;
    std::vector<ForceVector> Values;
    static void Validate(ForceVector value) {
        if (!std::isfinite(value.X) || !std::isfinite(value.Y))
            throw std::invalid_argument("force must be finite");
    }
    size_t Index(uint32_t x, uint32_t y) const {
        if (x >= Width || y >= Height)
            throw std::out_of_range("force coordinates are outside the world");
        return size_t(y) * Width + x;
    }
};
} // namespace pixel
