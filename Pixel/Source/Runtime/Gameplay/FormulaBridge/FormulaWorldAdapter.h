#pragma once

#include "Runtime/Simulation/World/World.h"
#include "Runtime/Gameplay/Actions/Mining.h"

namespace pixel {
struct FormulaVector { double X = 0, Y = 0; };
enum class FormulaContactKind { None, Solid, OutOfSimulation, NotReady };
struct FormulaContact {
    FormulaContactKind Kind = FormulaContactKind::None;
    double Fraction = 1;
    WorldPosition Position;
    CellCoord Cell;
};
struct FormulaDigResult {
    uint32_t Applied = 0, Blocked = 0, NotReady = 0, InventoryFull = 0;
};
enum class FormulaWindResult { Applied, ZeroRadius, BelowResolution, ZeroImpulse, Unavailable, BackendUnsupported, GridUnsupported };

// Geometry stays relative to an integer reference cell, including far from origin.
class FormulaWorldAdapter {
  public:
    static constexpr double BoltRadius = .35;
    static FormulaContact Sweep(const World &world, WorldPosition from, WorldPosition to, double radius = BoltRadius);
    static FormulaDigResult Dig(World &world, WorldPosition position, double radius,
                                const std::optional<MiningCollector> &collector = std::nullopt);
    static FormulaWindResult Wind(World &world, WorldPosition position, double radius, FormulaVector velocity);
    static FormulaWindResult WindAvailability(const World &world, WorldPosition position);
    static const char *WindResultName(FormulaWindResult result);
    static WorldPosition Offset(WorldPosition anchor, FormulaVector displacement);
};
} // namespace pixel
