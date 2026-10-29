#pragma once
#include "Runtime/System/Memory/ChunkedGrid.h"

#include "Dev/Debug/Performance/MemoryUsage.h"

#include "WindTypes.h"
#include "Runtime/Simulation/World/WorldMap.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace pixel {
struct WindSettings {
    WindBoundaryMode Boundary = WindBoundaryMode::Open;
    uint32_t Columns = 32;
    uint32_t Rows = 16;
    WindVelocity Ambient{90, 0}; // World cells/second; positive Y points down.
    float GustSpeed = 60;
    float Viscosity = 8;      // World cells squared/second.
    float Relaxation = 0.05f; // Per-second relaxation towards Ambient.
    uint32_t Seed = 20261004;
    float DirtDrag = 60; // Per-second drag at full projected blockage; zero disables feedback.
};

// A coarse MAC grid over [0, width) x [0, endRow). Dirt feedback uses
// directional projected blockage and a matching pressure solve. No renderer dependencies.
class WindField {
  public:
    performance::MemoryUsage GetMemoryUsage() const;

    void Reset(uint32_t width, uint32_t endRow, const WindSettings &settings = {});
    void MoveDomain(uint32_t width, uint32_t height, int64_t offsetX, int64_t offsetY);
    void Clear() { *this = {}; }
    bool IsEnabled() const { return !Velocity.empty(); }
    uint32_t GetEndRow() const { return Height; }
    const WindSettings &GetSettings() const { return Settings; }
    // One integration step, capped at 0.25s. Environment supplies fixed 1/60s.
    void Step(float seconds);
    // Bilinear sampling in world coordinates. Outside the wind region is calm.
    WindVelocity Sample(float x, float y) const;
    // A smooth velocity impulse; radius/position in world cells, delta in cells/s.
    // Open domains clip the impulse; periodic mode wraps it for numerical tests.
    void AddImpulse(float x, float y, float radius, WindVelocity delta);
    // Whether a nonzero component reaches a MAC face, before velocity clamping.
    bool ResolvesImpulse(float x, float y, float radius, WindVelocity delta) const;
    // Apply feedback once after a complete material step. No world storage is retained.
    void CoupleDirt(WorldMapView world, std::span<const DirtMotion> motion, float seconds);
    float RmsDivergence() const;

  private:
    WindSettings Settings;
    uint32_t Width = 0, Height = 0;
    float SpacingX = 1, SpacingY = 1;
    // X lives at (x, y+1/2), Y at (x+1/2, y). Shared periodic array extents.
    memory::ChunkedGrid<WindVelocity> Velocity, Previous;
    memory::ChunkedGrid<float> Pressure, Divergence;
    memory::ChunkedGrid<float> RightU, BottomV;
    memory::ChunkedGrid<float> DirtCoverage;
    memory::ChunkedGrid<WindVelocity> DirtVelocitySum;
    memory::ChunkedGrid<WindVelocity> DirtBlockage, PressureMobility;
    // Union of occupied rows/columns, so thickness cannot multiply frontal area.
    memory::ChunkedGrid<uint32_t> LastDirtRow;
    memory::ChunkedGrid<uint8_t> OccupiedColumns;
    memory::GridAddress Index(int x, int y) const;
    WindVelocity SampleGrid(const memory::ChunkedGrid<WindVelocity> &field, float x, float y) const;
    void Diffuse(float seconds);
    void Advect(float seconds);
    void Project(const memory::ChunkedGrid<WindVelocity> &mobility = {});
    bool Periodic() const { return Settings.Boundary == WindBoundaryMode::Periodic; }
    bool Inflow(WindBoundarySide side) const { return IsWindInflow(side, Settings.Ambient); }
    float UFace(int x, int y) const;
    float VFace(int x, int y) const;
    float ImpulseWeight(float x, float y, float radius, float faceX, float faceY) const;
    void PrescribeInflow();
    void ProjectOpen(const memory::ChunkedGrid<WindVelocity> &mobility);
    void AccumulateDirt(uint32_t x, uint32_t y, float coverage, WindVelocity velocity);
};
} // namespace pixel
