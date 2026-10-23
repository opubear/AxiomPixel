#pragma once

#include "CommitStamp.h"
#include "Devices/DeviceTypes.h"
#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace pixel::api {
using CallerSessionId = uint64_t;
struct Cell {
    int64_t X = 0, Y = 0;
    bool operator==(const Cell &) const = default;
};
struct Rect {
    Cell Origin;
    uint32_t Width = 0, Height = 0;
    bool operator==(const Rect &) const = default;
};
struct Position {
    Cell AnchorCell;
    std::array<double, 2> Offset{}; // Normalized to [0,1), in cells.
};
struct ProgramRef {
    uint64_t ResourceId = 1, Version = 0;
    bool operator==(const ProgramRef &) const = default;
};
enum class WindKind { Coarse, Reference, Procedural, Hybrid, Adaptive, Lbm, Empty };
struct RequestKey {
    uint64_t WorldEpoch = 0;
    CallerSessionId Caller = 0;
    uint64_t Sequence = 0;
    bool operator==(const RequestKey &) const = default;
};
struct FactId {
    uint64_t WorldEpoch = 0, Sequence = 0;
    bool operator==(const FactId &) const = default;
};
// Facts from one synchronous action are contiguous, including immediate effects.
// This fixed-size reference can describe a batch larger than the retained ring.
struct FactRange {
    uint64_t WorldEpoch = 0, FirstSequence = 0;
    uint32_t Count = 0;
};
struct InstanceRef {
    uint64_t WorldEpoch = 0, InstanceId = 0;
    bool operator==(const InstanceRef &) const = default;
};
struct ProgramEffectCause {
    InstanceRef Instance;
    ProgramRef Program;
    uint64_t EmissionSequence = 0;
    std::optional<RequestKey> ActivationRequest;
    std::optional<FactId> ParentFact;
};
struct DeviceCause { uint64_t DeviceId = 0, InstanceId = 0; };
struct AuthorityCause { uint64_t Version = 0; };
using FactCause = std::variant<RequestKey, ProgramEffectCause, DeviceCause, AuthorityCause>;

enum class DeviceEvent { Placed, Removed, Supplied, Enabled, StateChanged };
struct DeviceChanged { DeviceSnapshot Device; DeviceEvent Event; };

struct MaterialRemoved { Cell Target; uint32_t OldMaterial = 0, NewMaterial = 0; };
struct MaterialPlaced { Cell Target; uint32_t OldMaterial = 0, NewMaterial = 0; };
struct ProgramActivated { ProgramRef Program; uint64_t ActivationId = 0; };
struct WindConfiguration { WindKind Kind = WindKind::Empty; int64_t BoundaryY = 0; };
struct WindConfigured { WindConfiguration Before, After; };
// Position/radius describe the requested disk. Submitted does not claim any
// measured velocity change or that the disk covered a backend velocity face.
struct WindPulseSubmitted {
    Position Center;
    double RadiusCells = 0;
    std::array<double, 2> RequestedDeltaVelocityCellsPerSecond{};
    InstanceRef Instance;
};
// One bounded summary per successful disk, not an unbounded per-cell change log.
// The disk can be queried again to inspect its resulting materials.
struct MaterialRemovedInDisk {
    Position Center;
    double RadiusCells = 0;
    uint32_t Changed = 0, Blocked = 0, NotReady = 0;
};
enum class FieldSourceEvent { Installed, Updated, Stopped };
// An acknowledged operation, not a full source snapshot. Expiry and owner
// termination are reflected by the committed environment projection.
struct FieldSourceChanged { uint64_t Handle = 0; FieldSourceEvent Event; };
// An explicit player measurement. It grants neither program text nor control.
struct ForceObserved {
    uint64_t ActorId = 0;
    Cell Location;
    std::array<double, 2> AccelerationCellsPerSecondSquared{};
};
struct AuthorityChanged {
    uint64_t Version = 0, DeviceId = 0;
    Rect Region;
    bool Protected = false;
    bool ObservationAllowed = true, InterventionAllowed = true;
};
enum class InterventionStopReason { ControlRevoked, LeaseExpired };
struct InterventionStopped {
    InstanceRef Instance;
    CallerSessionId Subject = 0;
    InterventionStopReason Reason = InterventionStopReason::ControlRevoked;
};
using FactPayload = std::variant<MaterialRemoved, MaterialPlaced, ProgramActivated,
                                 WindConfigured, WindPulseSubmitted, MaterialRemovedInDisk, DeviceChanged, FieldSourceChanged, ForceObserved, AuthorityChanged, InterventionStopped>;
struct DomainFact {
    FactId Id;
    CommitStamp CommittedAt;
    FactCause Cause;
    FactPayload Payload;
};
enum class FactReadStatus { Available, Gap, StaleEpoch, InvalidRange, LimitExceeded };
struct FactReadResult {
    FactReadStatus Status = FactReadStatus::Available;
    uint64_t WorldEpoch = 0, HighWatermark = 0, FirstAvailableSequence = 1;
    // The last returned sequence, or the supplied exclusive lower bound.
    uint64_t NextSequence = 0;
    bool HasMore = false;
    std::vector<DomainFact> Facts;
};
} // namespace pixel::api
