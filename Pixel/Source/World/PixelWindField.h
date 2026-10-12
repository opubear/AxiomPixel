#pragma once

#include "WindTypes.h"
#include "WorldState.h"
#include <cstddef>

namespace pixel {
struct PixelWindSettings {
    WindVelocity Ambient{90, 0};
    float GustSpeed = 60;
    float Viscosity = 8;
    float Relaxation = 0.05f;
    uint32_t Seed = 20261004;
    double DivergenceTolerance = 1e-6; // Absolute RMS, pixels/s per pixel.
    uint32_t MaxPressureIterations = 4096;
};
struct WindSolveReport {
    double RmsDivergence = 0;
    double MaxDivergence = 0;
    uint32_t PressureIterations = 0;
    uint32_t Substeps = 0;
    bool Converged = true;
};

// Reference solver: one unit MAC cell per world pixel, periodic outer edges,
// impermeable stair-step solids with free-slip tangential boundaries. Double
// precision, CFL-limited semi-Lagrangian advection and converged PCG projection.
// Geometry is frozen during each step; moving solids update at step boundaries.
class PixelWindField {
  public:
    void Reset(uint32_t width, uint32_t endRow, const PixelWindSettings &settings = {});
    void Clear() { *this = {}; }
    bool IsEnabled() const { return !U.empty(); }
    uint32_t GetWidth() const { return Width; }
    uint32_t GetEndRow() const { return Height; }
    const PixelWindSettings &GetSettings() const { return Settings; }
    const WindSolveReport &GetReport() const { return Report; }
    void SetObstacles(WorldStateView world);
    void Step(float seconds);
    // Deterministic staggered initial conditions for cross-solver validation.
    // Inputs are row-major face velocities in pixels/s; projection follows.
    void SetFaceVelocities(std::span<const double> u, std::span<const double> v);
    void AddImpulse(float x, float y, float radius, WindVelocity delta);
    WindVelocity Sample(float x, float y) const;
    // Exact stored face values, for flux and no-penetration measurements.
    double FaceX(uint32_t x, uint32_t y) const;
    double FaceY(uint32_t x, uint32_t y) const;
    bool IsSolid(uint32_t x, uint32_t y) const;

  private:
    uint32_t Width = 0, Height = 0;
    PixelWindSettings Settings;
    WindSolveReport Report;
    std::vector<double> U, V, NextU, NextV;
    std::vector<uint8_t> Solid, OpenX, OpenY, SupportX, SupportY;
    std::vector<double> Pressure, Residual, Direction, Product, Preconditioned, Diagonal;
    size_t Index(int x, int y) const;
    size_t CheckedIndex(uint32_t x, uint32_t y) const;
    double SampleFace(const std::vector<double> &values, const std::vector<uint8_t> &open, double x, double y) const;
    void Transport(double dt);
    void ApplyMatrix(const std::vector<double> &input, std::vector<double> &output) const;
    void Project();
    void MeasureDivergence();
    void RebuildFaces();
};
} // namespace pixel
