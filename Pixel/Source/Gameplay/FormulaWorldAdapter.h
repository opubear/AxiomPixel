#pragma once

#include "Formula/Simulator.h"
#include "World/World.h"

namespace pixel {
enum class FormulaContactKind { None, Solid, OutOfSimulation, NotReady };
struct FormulaContact {
    FormulaContactKind Kind = FormulaContactKind::None;
    double Fraction = 1;
    WorldPosition Position;
    CellCoord Cell;
};
struct FormulaDigResult {
    uint32_t Applied = 0, Blocked = 0, NotReady = 0;
};
enum class FormulaWindResult { Applied, ZeroRadius, BelowResolution, ZeroImpulse, Unavailable, BackendUnsupported, GridUnsupported };

// Geometry stays relative to an integer reference cell, including far from origin.
class FormulaWorldAdapter {
  public:
    static constexpr double BoltRadius = .35;
    static FormulaContact Sweep(const World &world, WorldPosition from, WorldPosition to, double radius = BoltRadius);
    static FormulaDigResult Dig(World &world, WorldPosition position, double radius);
    static FormulaWindResult Wind(World &world, WorldPosition position, double radius, formula::Vec2 velocity);
    static const char *WindResultName(FormulaWindResult result);
    static WorldPosition Offset(WorldPosition anchor, formula::Vec2 displacement);
};
} // namespace pixel
