#pragma once

#include "Formula/Formula.h"
#include "FormulaWorldAdapter.h"
#include "GameAPI/Facts.h"
#include <deque>
#include <memory>
#include <span>
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
    uint64_t EffectRecordBytes = 0;
    uint64_t ReservedEffects = 0, ReservedWind = 0, RetiredWind = 0;
    uint64_t AcceptedThisFrame = 0, RejectedThisFrame = 0, WindCallsThisFrame = 0;
};
// Evidence of effects actually committed by the latest Cast/Step call. The
// Session supplies the authoritative epoch and complete commit stamp afterward.
struct FormulaEffectRecord {
    uint64_t CastId = 0, Version = 0, EmissionSequence = 0;
    std::optional<api::RequestKey> ActivationRequest;
    api::FactPayload Payload;
};

// Owns one independent local-coordinate Simulator per activation. Only the host's
// successful environment stage permits Step; paused casts flush their tick zero.
class FormulaRuntime {
  public:
    static constexpr size_t MaximumEffects = 256;
    FormulaRuntime();
    ~FormulaRuntime();
    FormulaRuntime(const FormulaRuntime &) = delete;
    FormulaRuntime &operator=(const FormulaRuntime &) = delete;
    bool Apply(const formula::Formula &description);
    const formula::Formula &ActiveFormula() const;
    uint64_t Version() const;
    uint64_t LastActivationId() const;
    const std::string &LastError() const;
    void BeginFrame();
    bool Cast(World &world, WorldPosition anchor, double angleDegrees,
              std::optional<api::RequestKey> activationRequest = std::nullopt);
    void Step(World &world);
    std::span<const FormulaEffectRecord> LastEffects() const;
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
