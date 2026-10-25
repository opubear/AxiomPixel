#pragma once

#include "Dev/Debug/Performance/MemoryUsage.h"
#include "Runtime/Simulation/World/WorldMap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace pixel {
// Permanent acceleration per world pixel; material motion never transports it.
// Environment composes temporary FieldSources without changing these values.
class ForceField {
  public:
    performance::MemoryUsage GetMemoryUsage() const {
        if (Map)
            return Map->GetForceMemoryUsage();
        return performance::MeasureObject(*this, Values);
    }

    ForceField(uint32_t width, uint32_t height) : Width(width), Height(height) {
        const uint64_t count = uint64_t(width) * height;
        if (!width || !height || count > Values.max_size())
            throw std::invalid_argument("force field dimensions must fit a nonempty array");
        Values.resize(size_t(count));
    }
    explicit ForceField(WorldMap &map) : Width(map.Bounds().Width), Height(map.Bounds().Height), Map(&map) {}
    uint32_t GetWidth() const { return Width; }
    uint32_t GetHeight() const { return Height; }
    ForceVector Get(int64_t x, int64_t y) const { return Map ? Map->GetForce({x, y}) : Values.at(Index(x, y)); }
    void Set(int64_t x, int64_t y, ForceVector value) {
        Validate(value);
        if (Map)
            Map->SetForce({x, y}, value);
        else
            Values.at(Index(x, y)) = value;
    }
    void Add(int64_t x, int64_t y, ForceVector value) {
        const auto old = Get(x, y);
        Set(x, y, {old.X + value.X, old.Y + value.Y});
    }
    void Fill(ForceVector value) {
        Validate(value);
        if (Map) {
            if (!Map->IsFinite())
                throw std::logic_error("unbounded forces require an explicit region");
            Map->FillForce(Map->Bounds(), value);
            return;
        }
        std::fill(Values.begin(), Values.end(), value);
    }

  private:
    uint32_t Width, Height;
    WorldMap *Map = nullptr;
    axiom::Array<ForceVector> Values;
    static void Validate(ForceVector value) {
        if (!std::isfinite(value.X) || !std::isfinite(value.Y))
            throw std::invalid_argument("force must be finite");
    }
    size_t Index(int64_t x, int64_t y) const {
        if (x < 0 || y < 0 || x >= Width || y >= Height)
            throw std::out_of_range("force coordinates are outside the world");
        return size_t(y) * Width + x;
    }
};
} // namespace pixel
