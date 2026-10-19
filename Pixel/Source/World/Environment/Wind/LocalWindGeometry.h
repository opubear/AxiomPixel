#pragma once

#include "Performance/MemoryUsage.h"
#include "WindStrategy.h"

namespace pixel {
// Bounded nearest-obstacle model shared by the two approximate strategies.
// It constrains samples, not a pressure solve: no claim of incompressibility or
// correct flux through narrow channels. Interior pixels create no extra wakes.
class LocalWindGeometry {
  public:
    performance::MemoryUsage GetMemoryUsage() const;

    LocalWindGeometry(uint32_t width, uint32_t height, const LocalWindSettings &settings, WindBoundaryMode boundary = WindBoundaryMode::Open);
    void Update(WorldMapView world);
    bool IsSolid(uint32_t x, uint32_t y) const;
    WindVelocity Constrain(float x, float y, WindVelocity incoming) const;
    uint32_t GetWidth() const { return Width; }
    uint32_t GetEndRow() const { return Height; }

  private:
    uint32_t Width, Height;
    LocalWindSettings Settings;
    WindBoundaryMode Boundary;
    std::vector<uint8_t> Solid;
    std::vector<int32_t> Nearest;
    std::vector<uint32_t> Queue;
    void RebuildDistance();
};
} // namespace pixel
