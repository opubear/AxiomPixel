#pragma once
#include "Formula/Process/Program.h"
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
