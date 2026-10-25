#pragma once
#include "Formula.h"
#include <variant>

namespace pixel::formula {
struct Vec2 {
    double X = 0, Y = 0;
};
using ProjectileId = std::uint64_t;
using CastId = std::uint64_t;
struct BoltSpawn {
    ProjectileId Id;
    Vec2 Velocity;
    double Damage;
    Tick Lifetime;
};
struct WindImpulse {
    double Radius;
    Vec2 Velocity;
};
struct DigArea {
    double Radius;
};
struct ProjectileHit {
    ProjectileId Id;
};
struct ProjectileExpired {
    ProjectileId Id;
};
struct Diagnostic {
    std::string Message;
};
using Event = std::variant<BoltSpawn, WindImpulse, DigArea, ProjectileHit, ProjectileExpired, Diagnostic>;
struct TraceEntry {
    Tick At;
    CastId Cast;
    Vec2 Position;
    Event Data;
};
struct ProjectileView {
    ProjectileId Id;
    CastId Cast;
    Vec2 Position, Velocity;
    double Damage;
    Tick ExpiresAt;
};
struct SimulatorState;

// Independent, deterministic 60 Hz clock. It emits commands and integrates
// straight trajectories analytically; the host supplies collision events.
class Simulator {
  public:
    Simulator();
    ~Simulator();
    Simulator(const Simulator &) = delete;
    Simulator &operator=(const Simulator &) = delete;

    // Queues activation at Now(). AdvanceTo(Now()) flushes it.
    CastId Cast(const Formula &formula, Vec2 origin = {}, double angleDegrees = 0);
    void AdvanceTo(Tick tick);
    // Consumes one live carrier, scheduling its payloads at Now()+1.
    // An unknown, already hit or expired projectile returns false.
    bool Hit(ProjectileId id, Vec2 position);
    Tick Now() const;
    std::vector<ProjectileView> Projectiles() const;
    const std::vector<TraceEntry> &Trace() const;
    std::vector<TraceEntry> DrainTrace();
    bool IsIdle() const;
    std::size_t PendingCount() const;
    // Removes a carrier without a hit, expiration event, or payload activation.
    // Its already queued expiration remains pending until that tick is reached.
    bool DiscardProjectile(ProjectileId id);

  private:
    std::unique_ptr<SimulatorState> State;
};
std::string FormatTrace(const TraceEntry &entry);
} // namespace pixel::formula
