#pragma once
#include "Formula/Process/Program.h"
#include "Formula/Process/Value.h"
#include <array>
#include <span>

namespace pixel::formula::process {
struct ExecutionLimits {
    std::uint32_t Work = 256;
    std::uint32_t Queries = 8;
    std::uint32_t Outputs = 16;
};
enum class Phase { Ready, Sleeping, Awaiting, Ended };
struct InstanceState {
    InstanceId Id = 0;
    Phase Status = Phase::Ready;
    std::size_t Instruction = 0;
    std::uint64_t ActiveSteps = 0, Remaining = 0;
    RequestId LastRequest;
    Outcome LastOutcome = Outcome::None;
    Position Origin;
    std::optional<Subscription> WakeSubscription;
};
// Export only at Sleeping/Awaiting, after the host commits the returned slice.
// Host-owned carriers, queued events and committed world facts are separate.
// v1 has no random operation/state. Adding one requires a new schema/semantics.
struct InstanceSnapshot {
    std::uint32_t SchemaVersion = 1;
    ProgramIdentity Program;
    InstanceState State;
    ExecutionLimits Limits;
    std::array<std::optional<Value>, 16> Locals;
    Boundary LastBoundary = 0;
    std::uint64_t EventSequence = 0, SubscriptionSequence = 0;
    AwaitKind WaitingFor = AwaitKind::Result;
    std::optional<HostEvent> Receipt, Impact, Wake;
    bool ReceiptSeen = false, ImpactSeen = false;
};
struct StepResult {
    std::array<EffectRequest, 64> Requests;
    std::size_t Count = 0;
    std::uint32_t WorkUsed = 0, QueriesUsed = 0;
    std::optional<Diagnostic> Fault;
    std::optional<Termination> End;
    std::span<const EffectRequest> Effects() const { return {Requests.data(), Count}; }
};

class Instance {
  public:
    // Bad IDs/limits/positions are host programming errors (invalid_argument).
    // A missing static capability is an expected admission refusal (Diagnostic).
    static std::variant<std::unique_ptr<Instance>, Diagnostic> Start(
        ProgramArtifact program, InstanceId id, Capabilities granted = AllCapabilities,
        ExecutionLimits limits = {}, Position origin = {});
    ~Instance();
    Instance(const Instance &) = delete;
    Instance &operator=(const Instance &) = delete;

    const ProgramArtifact &Program() const;
    const InstanceState &State() const;
    // The owner must stop/destroy the source instance before restoring it.
    // Resume uses the same host boundary space; the next Step must be later.
    InstanceSnapshot ExportState() const;
    static std::variant<std::unique_ptr<Instance>, Diagnostic> Restore(
        ProgramArtifact program, const InstanceSnapshot &snapshot, Capabilities granted = AllCapabilities);
    // At most 32 delivered events, committed before view.At, ordered by sequence.
    // Invalid host contracts throw; language faults return a terminal diagnostic.
    StepResult Step(const HostView &view, std::span<const HostEvent> events = {});
    StepResult Cancel();

  private:
    struct Execution;
    explicit Instance(std::unique_ptr<Execution> execution);
    std::unique_ptr<Execution> Run;
};
} // namespace pixel::formula::process
