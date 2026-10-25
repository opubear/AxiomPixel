#pragma once
#include "FormulaWorldAdapter.h"
#include "Runtime/Gameplay/GameAPI/Facts.h"
#include <string>

namespace pixel {
struct FormulaProjectile {
    uint64_t WorldEpoch = 0, CastId = 0, ProjectileId = 0, Version = 0;
    WorldPosition Position;
    double Radius = FormulaWorldAdapter::BoltRadius, Damage = 0;
    FormulaVector Velocity;
    uint64_t ResourceId = 2;
};
struct FormulaRuntimeDiagnostic {
    uint64_t WorldEpoch = 0, CastId = 0;
    uint64_t Tick = 0;
    std::string Message;
};
struct FormulaRuntimeStats {
    size_t Roots = 0, Projectiles = 0, Pending = 0;
    // Tracked retained presentation payload capacity; excludes allocator bookkeeping.
    uint64_t SnapshotBytes = 0, DiagnosticBytes = 0;
    uint64_t EffectRecordBytes = 0;
    uint64_t AcceptedThisFrame = 0, RejectedThisFrame = 0;
};
// Evidence of effects actually committed by the latest Cast/Step call. The
// Session supplies the authoritative epoch and complete commit stamp afterward.
struct FormulaEffectRecord {
    uint64_t CastId = 0, Version = 0, EmissionSequence = 0;
    std::optional<api::RequestKey> ActivationRequest;
    api::FactPayload Payload;
    uint64_t ResourceId = 2;
};

} // namespace pixel
