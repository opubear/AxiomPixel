#pragma once

#include "Dev/Debug/Performance/MemoryUsage.h"
#include "WindField.h"

namespace pixel {
// Analytic divergence-free shear gusts in empty air. Impulses are a decaying
// sampled velocity grid; geometry constraints are applied by the strategy.
class ProceduralWindField {
  public:
    performance::MemoryUsage GetMemoryUsage() const;

    ProceduralWindField(uint32_t width, uint32_t height, const WindSettings &settings);
    void Step(float seconds);
    void MoveDomain(uint32_t width, uint32_t height, CellCoord origin);
    void SetOrigin(CellCoord origin) { Origin = origin; }
    void SetTime(double seconds) { Time = seconds; }
    void AddImpulse(float x, float y, float radius, WindVelocity delta);
    WindVelocity Sample(float x, float y) const;

  private:
    uint32_t Width, Height;
    // A fixed world wavelength keeps clipping/recreation from changing phase.
    static constexpr uint32_t GustWavelength = 1024;
    CellCoord Origin{};
    WindSettings Settings;
    double Time = 0, Phase = 0;
    std::vector<WindVelocity> Impulses;
};
} // namespace pixel
