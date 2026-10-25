#pragma once

#include "Dev/Debug/Performance/MemoryUsage.h"
#include "WindTypes.h"
#include "Runtime/Simulation/World/WorldMap.h"
#include <array>
#include <vector>

namespace pixel {
struct LbmWindSettings {
    WindBoundaryMode Boundary = WindBoundaryMode::Open;
    WindVelocity Ambient{30, 0};
    float GustSpeed = 10;
    float Viscosity = 8;
    float Relaxation = .05f;
    uint32_t Seed = 20261004;
    double LatticeSeconds = 1.0 / 600;
};
struct LbmWindReport {
    double MinDensity = 1, MaxDensity = 1, Mass = 0;
    uint32_t Substeps = 0;
    uint64_t Stabilizations = 0; // Cumulative velocity limits/positivity recoveries.
    double EffectiveViscosity = 0;
};
// CPU D2Q9 BGK prototype, open/periodic streaming and halfway solid bounce-back.
// Fixed lattice time, low-Mach velocity cap, reported positivity recovery. This
// is weakly compressible and is not an incompressible reference or GPU backend.
class LbmWindField {
  public:
    performance::MemoryUsage GetMemoryUsage() const;

    LbmWindField(WorldMapView world, uint32_t height, const LbmWindSettings &settings);
    void SetObstacles(WorldMapView world);
    void MoveDomain(WorldMapView world, uint32_t height, int64_t offsetX, int64_t offsetY);
    void Step(float seconds);
    void AddImpulse(float x, float y, float radius, WindVelocity delta);
    WindVelocity Sample(float x, float y) const;
    bool IsSolid(uint32_t x, uint32_t y) const;
    uint32_t GetWidth() const { return Width; }
    uint32_t GetEndRow() const { return Height; }
    const LbmWindReport &GetReport() const { return Report; }

  private:
    using Distribution = std::array<double, 9>;
    uint32_t Width, Height;
    LbmWindSettings Settings;
    LbmWindReport Report;
    double Remainder = 0, Tau = .6;
    std::vector<uint8_t> Solid;
    std::vector<Distribution> F, Next;
    std::vector<WindVelocity> Velocity;
    Distribution Equilibrium(double rho, WindVelocity velocity);
    void CollideAndStream();
    void Measure();
    void ApplyOpenBoundaries();
    void ReconstructBoundary(uint32_t x, uint32_t y, int normalX, int normalY);
};
} // namespace pixel
