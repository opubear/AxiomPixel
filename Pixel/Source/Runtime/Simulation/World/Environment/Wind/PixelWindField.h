#pragma once
#include "Runtime/System/Memory/ChunkedGrid.h"

#include "Dev/Debug/Performance/MemoryUsage.h"

#include "WindTypes.h"
#include "Runtime/Simulation/World/WorldMap.h"
#include <cstddef>
#include <span>
#include <vector>

namespace pixel {
struct PixelWindSettings {
    WindBoundaryMode Boundary = WindBoundaryMode::Open;
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

// Reference solver: one unit MAC cell per world pixel, open or periodic edges,
// impermeable stair-step solids with free-slip tangential boundaries. Double
// precision, CFL-limited semi-Lagrangian advection and converged PCG projection.
// Geometry is frozen during each step; moving solids update at step boundaries.
class PixelWindField {
  public:
    performance::MemoryUsage GetMemoryUsage() const;

    void Reset(uint32_t width, uint32_t endRow, const PixelWindSettings &settings = {});
    void Clear() { *this = {}; }
    bool IsEnabled() const { return !U.empty(); }
    uint32_t GetWidth() const { return Width; }
    uint32_t GetEndRow() const { return Height; }
    const PixelWindSettings &GetSettings() const { return Settings; }
    const WindSolveReport &GetReport() const { return Report; }
    void SetObstacles(WorldMapView world);
    void MoveDomain(WorldMapView world, uint32_t height, int64_t offsetX, int64_t offsetY);
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
    memory::ChunkedGrid<double> U, V, NextU, NextV;
    memory::ChunkedGrid<double> RightU, BottomV;
    memory::ChunkedGrid<uint8_t> Solid, OpenX, OpenY, SupportX, SupportY;
    memory::ChunkedGrid<double> Pressure, Residual, Direction, Product, Preconditioned, Diagonal;
    memory::GridAddress Index(int x, int y) const;
    memory::GridAddress CheckedIndex(uint32_t x, uint32_t y) const;
    double SampleFace(const memory::ChunkedGrid<double> &values, const memory::ChunkedGrid<uint8_t> &open, double x, double y) const;
    void Transport(double dt);
    void ApplyMatrix(const memory::ChunkedGrid<double> &input, memory::ChunkedGrid<double> &output) const;
    void Project();
    void MeasureDivergence();
    void RebuildFaces();
    bool Periodic() const { return Settings.Boundary == WindBoundaryMode::Periodic; }
    bool Inflow(WindBoundarySide side) const { return IsWindInflow(side, Settings.Ambient); }
    double PressureWeight(int x, int y, int dx, int dy) const;
    double UFace(int x, int y) const;
    double VFace(int x, int y) const;
    double DivergenceAt(int x, int y) const;
    void PrescribeInflow();
};
} // namespace pixel
