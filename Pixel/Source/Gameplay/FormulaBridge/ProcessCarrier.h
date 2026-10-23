#pragma once
#include "Formula/Process/Runtime.h"
#include "World/WorldCoordinates.h"
#include <vector>

namespace pixel {
enum class ProcessCarrierKind { Player, NaturalSource, FixedFixture, Device };

// Trusted host assembly, never supplied by a language program. Resources are
// finite per-activation credits; the device owner pays its Items fuel before launch.
struct ProcessCarrier {
    ProcessCarrierKind Kind = ProcessCarrierKind::Player;
    uint64_t Subject = 1;
    WorldPosition Origin;
    WorldRect Scope;
    double AngleDegrees = 0;
    double MaximumRadius = 16;
    // Fixed carriers may require a complete operating footprint even while
    // waiting between effects. Ordinary casts require only their anchor.
    double ActivityRadius = 0;
    int64_t Credits = 64;
    formula::process::Capabilities Grants = formula::process::AllCapabilities;
    formula::process::ExecutionLimits Limits{};
};
enum class ProcessRefusal {
    None, Capability, Scope, Geometry, Resources, NotReady, Blocked,
    WindUnavailable, Capacity, InvalidHandle
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
    size_t SuspendedInstances = 0;
    uint64_t Work = 0, Queries = 0, Requests = 0, Accepted = 0, Rejected = 0;
    uint64_t RetainedBytes = 0;
};
struct ProcessInspection {
    formula::process::ProgramIdentity Program;
    formula::process::InstanceState State;
    uint64_t Version = 0;
    ProcessCarrierKind Carrier;
    uint64_t Subject = 0;
    int64_t Credits = 0;
    bool Suspended = false;
    uint64_t ResourceId = 2;
    ProcessCarrier Configuration;
    // Missing handles may have expired or been stopped. An existing source
    // referenced here must belong to this instance, never another owner.
    std::vector<uint64_t> ReferencedFieldHandles;
};
} // namespace pixel
