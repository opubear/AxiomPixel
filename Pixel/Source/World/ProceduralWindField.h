#pragma once
#include "WindField.h"

namespace pixel {
// Analytic divergence-free shear gusts in empty air. Impulses are a decaying
// sampled velocity grid; geometry constraints are applied by the strategy.
class ProceduralWindField {
  public:
    ProceduralWindField(uint32_t width, uint32_t height, const WindSettings &settings);
    void Step(float seconds);
    void AddImpulse(float x, float y, float radius, WindVelocity delta);
    WindVelocity Sample(float x, float y) const;

  private:
    uint32_t Width, Height;
    WindSettings Settings;
    double Time = 0, Phase = 0;
    std::vector<WindVelocity> Impulses;
};
} // namespace pixel
