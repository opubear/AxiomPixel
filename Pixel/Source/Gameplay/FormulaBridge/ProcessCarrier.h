#pragma once
#include "Formula/Process/Runtime.h"
#include "World/WorldCoordinates.h"

namespace pixel {
enum class ProcessCarrierKind { Player, NaturalSource, FixedFixture };

// Trusted host assembly, never supplied by a language program. Resources are
// finite per-activation fixture credits, not the future Items inventory.
struct ProcessCarrier {
    ProcessCarrierKind Kind = ProcessCarrierKind::Player;
    uint64_t Subject = 1;
    WorldPosition Origin;
    WorldRect Scope;
    double AngleDegrees = 0;
    double MaximumRadius = 16;
    int64_t Credits = 64;
    formula::process::Capabilities Grants = formula::process::AllCapabilities;
    formula::process::ExecutionLimits Limits{};
};
enum class ProcessRefusal {
    None, Capability, Scope, Geometry, Resources, NotReady, Blocked,
    WindUnavailable, Capacity
};
struct ProcessReceipt {
    formula::process::Boundary At = 0;
    formula::process::RequestId Request;
    ProcessCarrierKind Carrier;
    uint64_t Subject = 0;
    formula::process::Outcome Outcome;
    ProcessRefusal Refusal = ProcessRefusal::None;
    uint32_t ChangedCells = 0;
};
struct ProcessHostStats {
    size_t Instances = 0, Projectiles = 0, PendingEvents = 0;
    uint64_t Work = 0, Queries = 0, Requests = 0, Accepted = 0, Rejected = 0;
    uint64_t RetainedBytes = 0;
};
} // namespace pixel
