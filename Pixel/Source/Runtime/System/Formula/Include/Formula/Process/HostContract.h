#pragma once
#include <cstdint>
#include <optional>
#include <variant>

namespace pixel::formula::process {
using InstanceId = std::uint64_t;
using Boundary = std::uint64_t;

struct SourceSpan {
    std::size_t Offset = 0, Length = 0, Line = 1, Column = 1;
    bool operator==(const SourceSpan &) const = default;
};

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
struct EffectHandle {
    std::uint64_t Id = 0;
    bool operator==(const EffectHandle &) const = default;
};
// Acceleration is measured in cells/s²; it is not a wind velocity impulse.
struct FieldSource {
    double Radius = 0, AccelerationX = 0, AccelerationY = 0;
    std::uint64_t Lifetime = 1;
    bool operator==(const FieldSource &) const = default;
};
struct UpdateField {
    EffectHandle Handle{};
    FieldSource Source;
    bool operator==(const UpdateField &) const = default;
};
struct StopField {
    EffectHandle Handle{};
    bool operator==(const StopField &) const = default;
};
using Effect = std::variant<Dig, Wind, Bolt, FieldSource, UpdateField, StopField>;

enum class Capability : std::uint32_t {
    Dig = 1, Wind = 2, Bolt = 4, Inventory = 8, Wake = 16, Field = 32
};
using Capabilities = std::uint32_t;
constexpr Capabilities AllCapabilities = 63;
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
    SourceSpan Source;
};

// One frozen value per active boundary. Evaluate every instance before applying
// any returned effects. Skipped boundaries do not advance an instance's clock.
struct HostView {
    Boundary At = 0;
    std::optional<std::int64_t> Inventory;
    // Optional live default context. Pinned hit scopes ignore binding movement.
    std::optional<Position> Origin;
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
    // Successful field installation/update returns its host-owned opaque token.
    // Other events carry zero. The host verifies ownership on update/stop.
    EffectHandle Handle{};
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
// be called when an active instance is removed, unless ownership is explicitly
// transferred to a suspension capsule. Destroying C++ state is not cleanup.
// Suspension stores owned effects and undelivered events alongside the language
// snapshot after commit. World-owned entities and one-shot facts stay with the
// world; restore must not reissue their original requests.
} // namespace pixel::formula::process
