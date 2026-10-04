#pragma once

#include "WorldState.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pixel {
struct WindVelocity {
    float X = 0;
    float Y = 0;
    bool operator==(const WindVelocity &) const = default;
};

struct WindSettings {
    uint32_t Columns = 32;
    uint32_t Rows = 16;
    WindVelocity Ambient{90, 0}; // World cells/second; positive Y points down.
    float GustSpeed = 60;
    float Viscosity = 8;      // World cells squared/second.
    float Relaxation = 0.05f; // Per-second relaxation towards Ambient.
    uint32_t Seed = 20261004;
    float DirtDrag = 60; // Per-second drag at full projected blockage; zero disables feedback.
};

// Actual velocity of a moved dirt cell, recorded at its final position. Supply
// one entry per moved cell; occupied cells without an entry are stationary.
struct DirtMotion {
    uint32_t X, Y;
    WindVelocity Velocity;
};

// A coarse, periodic MAC grid over [0, width) x [0, endRow). Dirt feedback uses
// directional projected blockage and a matching pressure solve. No renderer dependencies.
class WindField {
  public:
    void Reset(uint32_t width, uint32_t endRow, const WindSettings &settings = {});
    void Clear() { *this = {}; }
    bool IsEnabled() const { return !Velocity.empty(); }
    uint32_t GetEndRow() const { return Height; }
    const WindSettings &GetSettings() const { return Settings; }
    // One integration step, capped at 0.25s. Environment supplies fixed 1/60s.
    void Step(float seconds);
    // Bilinear sampling in world coordinates. Outside the wind region is calm.
    WindVelocity Sample(float x, float y) const;
    // A smooth velocity impulse; radius/position in world cells, delta in cells/s.
    // Periodic distance makes an impulse crossing an edge continuous.
    void AddImpulse(float x, float y, float radius, WindVelocity delta);
    // Apply feedback once after a complete material step. No world storage is retained.
    void CoupleDirt(WorldStateView world, std::span<const DirtMotion> motion, float seconds);
    float RmsDivergence() const;

  private:
    WindSettings Settings;
    uint32_t Width = 0, Height = 0;
    float SpacingX = 1, SpacingY = 1;
    // X lives at (x, y+1/2), Y at (x+1/2, y). Shared periodic array extents.
    std::vector<WindVelocity> Velocity, Previous;
    std::vector<float> Pressure, Divergence;
    std::vector<float> DirtCoverage;
    std::vector<WindVelocity> DirtVelocitySum;
    std::vector<WindVelocity> DirtBlockage, PressureMobility;
    // Union of occupied rows/columns, so thickness cannot multiply frontal area.
    std::vector<uint32_t> LastDirtRow;
    std::vector<uint8_t> OccupiedColumns;
    size_t Index(int x, int y) const;
    WindVelocity SampleGrid(const std::vector<WindVelocity> &field, float x, float y) const;
    void Diffuse(float seconds);
    void Advect(float seconds);
    void Project(std::span<const WindVelocity> mobility = {});
    void AccumulateDirt(uint32_t x, uint32_t y, float coverage, WindVelocity velocity);
};
} // namespace pixel
