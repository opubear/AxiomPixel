#pragma once
#include <cstdint>
#include <optional>
#include <variant>

namespace pixel::formula::process {
using InstanceId = std::uint64_t;
using Boundary = std::uint64_t;

// Preserve distant cell coordinates without converting them to floating point.
struct Position {
    std::int64_t CellX = 0, CellY = 0;
    double OffsetX = 0, OffsetY = 0;
    bool operator==(const Position &) const = default;
};
struct Dig {
    double Radius = 0;
    bool operator==(const Dig &) const = default;
};
struct Wind {
    double Radius = 0, VelocityX = 0, VelocityY = 0;
    bool operator==(const Wind &) const = default;
};
struct Bolt {
    double Damage = 0, VelocityX = 0;
    std::uint64_t Lifetime = 1;
    bool operator==(const Bolt &) const = default;
};
using Effect = std::variant<Dig, Wind, Bolt>;

enum class Capability : std::uint32_t {
    Dig = 1, Wind = 2, Bolt = 4, Inventory = 8, Wake = 16
};
using Capabilities = std::uint32_t;
constexpr Capabilities AllCapabilities = 31;
constexpr Capabilities Mask(Capability capability) { return static_cast<Capabilities>(capability); }

struct RequestId {
    InstanceId Instance = 0;
    std::uint64_t Sequence = 0;
    bool operator==(const RequestId &) const = default;
};
struct EffectRequest {
    RequestId Id;
    Position Origin;
    Effect Description;
};

// One frozen value per active boundary. Evaluate every instance before applying
// any returned effects. Skipped boundaries do not advance an instance's clock.
struct HostView {
    Boundary At = 0;
    std::optional<std::int64_t> Inventory;
};
enum class Outcome { None, Applied, Rejected, Hit, Expired, Cancelled, Wake, Timeout };
enum class AwaitKind { Result, Hit, Wake };
struct Subscription {
    InstanceId Instance = 0;
    std::uint64_t Sequence = 0;
    bool operator==(const Subscription &) const = default;
};
struct HostEvent {
    // Monotonically increasing per target instance; replayed sequences are ignored.
    std::uint64_t Sequence = 0;
    Boundary CommittedAt = 0;
    InstanceId Target = 0;
    Outcome Result = Outcome::None;
    RequestId Request;
    Subscription WakeSubscription;
    Position HitPosition;
};
// Process v1 can address only its latest emitted RequestId and current wake
// Subscription. A host may omit notifications for retired tokens, while keeping
// their already-applied facts and owned carriers until normal expiry/cleanup.
enum class EndReason { Completed, Stopped, Cancelled, Faulted };
struct Termination {
    InstanceId Instance = 0;
    EndReason Reason = EndReason::Completed;
};
// On termination the host first commits this slice's requests, then removes all
// owned carriers, persistent effects, subscriptions and pending work. Applied
// one-shot facts survive. Faulted slices contain no requests. Cancel must also
// be called when an active instance is removed; destroying C++ state is not cleanup.
} // namespace pixel::formula::process
