#pragma once

#include "Formula/Formula.h"
#include "FormulaWorldAdapter.h"
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace pixel {
struct FormulaProjectile {
    uint64_t WorldEpoch = 0, CastId = 0, ProjectileId = 0, Version = 0;
    WorldPosition Position;
    double Radius = FormulaWorldAdapter::BoltRadius, Damage = 0;
    formula::Vec2 Velocity;
};
struct FormulaRuntimeDiagnostic {
    uint64_t WorldEpoch = 0, CastId = 0;
    formula::Tick Tick = 0;
    std::string Message;
};
struct FormulaRuntimeStats {
    size_t Roots = 0, Projectiles = 0, Pending = 0;
    // Tracked retained payload capacity only: excludes Simulator internals,
    // shared Formula nodes, deque blocks, and allocator bookkeeping.
    uint64_t SnapshotBytes = 0, DiagnosticBytes = 0;
    uint64_t ReservedEffects = 0, ReservedWind = 0, RetiredWind = 0;
    uint64_t AcceptedThisFrame = 0, RejectedThisFrame = 0, WindCallsThisFrame = 0;
};

// Owns one independent local-coordinate Simulator per activation. Only the host's
// successful fixed-step callback calls Step; paused casts flush their tick zero.
class FormulaRuntime {
  public:
    FormulaRuntime();
    ~FormulaRuntime();
    FormulaRuntime(const FormulaRuntime &) = delete;
    FormulaRuntime &operator=(const FormulaRuntime &) = delete;
    bool Apply(const formula::Formula &description);
    const formula::Formula &ActiveFormula() const;
    uint64_t Version() const;
    const std::string &LastError() const;
    void BeginFrame();
    bool Cast(World &world, WorldPosition anchor, double angleDegrees);
    void Step(World &world);
    void Clear();
    void OnWorldReplaced();
    const std::vector<FormulaProjectile> &Projectiles() const;
    const std::deque<FormulaRuntimeDiagnostic> &Diagnostics() const;
    FormulaRuntimeStats Stats() const;
    uint64_t WorldEpoch() const;

  private:
    struct State;
    std::unique_ptr<State> Data;
};
} // namespace pixel
