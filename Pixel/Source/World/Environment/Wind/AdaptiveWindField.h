#pragma once

#include "Performance/MemoryUsage.h"
#include "PixelWindField.h"

namespace pixel {
struct AdaptiveWindSettings {
    PixelWindSettings Flow;
    uint32_t BlockSize = 8; // 1..32; mixed blocks recursively split to pixels.
};
// Finite-volume prototype on rectangular leaves. A single graph pressure solve
// shares fluxes across coarse/fine interfaces. Geometry is exact at pixel edges;
// transport is first-order and cell-centred, not the reference MAC discretization.
class AdaptiveWindField {
  public:
    performance::MemoryUsage GetMemoryUsage() const;

    AdaptiveWindField(WorldMapView world, uint32_t height, const AdaptiveWindSettings &settings);
    void SetObstacles(WorldMapView world);
    void MoveDomain(WorldMapView world, uint32_t height, int64_t offsetX, int64_t offsetY);
    void Step(float seconds);
    // Row-major pixel velocities are volume-averaged into leaves, then projected.
    void SetVelocities(std::span<const WindVelocity> velocities);
    void AddImpulse(float x, float y, float radius, WindVelocity delta);
    WindVelocity Sample(float x, float y) const;
    bool IsSolid(uint32_t x, uint32_t y) const;
    uint32_t GetWidth() const { return Width; }
    uint32_t GetEndRow() const { return Height; }
    size_t GetLeafCount() const { return Cells.size(); }
    const WindSolveReport &GetReport() const { return Report; }

  private:
    struct Cell {
        uint32_t X, Y, W, H;
        WindVelocity Velocity;
        double Volume() const { return double(W) * H; }
    };
    struct Face {
        uint32_t A, B;
        bool Horizontal;
        double Length, Distance, Velocity;
    };
    uint32_t Width, Height;
    AdaptiveWindSettings Settings;
    WindSolveReport Report;
    std::vector<uint8_t> Solid;
    std::vector<int32_t> Owner;
    std::vector<Cell> Cells;
    struct BoundaryFace {
        uint32_t Owner;
        bool Horizontal, Positive, Inflow;
        double Length, Distance, Velocity;
    };
    std::vector<Face> Faces;
    std::vector<BoundaryFace> BoundaryFaces;
    void Split(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const std::vector<WindVelocity> &previous);
    void BuildFaces();
    void InitializeFaceVelocities();
    void ReconstructVelocities();
    void Project();
    void Transport(double dt);
};
} // namespace pixel
