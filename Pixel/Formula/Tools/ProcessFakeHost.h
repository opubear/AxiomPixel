#pragma once
#include "Formula/Process/Runtime.h"
#include <vector>

namespace pixel::formula::process::testing {
struct TraceEntry { Boundary At; EffectRequest Request; Outcome Result; };
struct Carrier { EffectRequest Request; std::uint64_t Remaining; };
struct SuspendedProcess {
    ProgramRecord Program;
    InstanceSnapshot Runtime;
    std::uint64_t NextEvent = 1;
    std::vector<HostEvent> Events;
    std::vector<Carrier> Carriers;
};

// One-instance executable reference host. Its bounded operational state is
// separate from the deliberately retained trace used by tests and the CLI.
class FakeHost {
  public:
    explicit FakeHost(std::unique_ptr<Instance> instance);
    StepResult Advance();
    void Skip(std::uint64_t boundaries); // inactive region: no local aging
    void Hit(Position position);
    void Wake(Outcome outcome = Outcome::Wake);
    void Cancel();
    // Owns one continuation exclusively. Suspension destroys the interpreter;
    // Resume recompiles its record and consumes the capsule exactly once.
    void Suspend();
    std::optional<Diagnostic> Resume(Capabilities granted = AllCapabilities);
    const SuspendedProcess &Suspension() const;
    // Reference ownership transfer: an independent world entity no longer
    // sends process callbacks and is not removed by process cleanup.
    void ReleaseCarrier(RequestId request);

    std::int64_t Inventory = 0;
    bool RejectEffects = false;
    const Instance &Process() const;
    const std::vector<TraceEntry> &Trace() const { return History; }
    std::size_t OwnedCount() const { return Carriers.size(); }
    std::size_t IndependentCount() const { return Independent.size(); }
    std::size_t PendingCount() const { return Events.size(); }
    std::size_t CleanupCount() const { return Cleanups; }
    Boundary Now() const { return At; }

  private:
    void Queue(Outcome outcome, RequestId request = {}, Subscription subscription = {}, Position position = {});
    void Apply(const StepResult &result);
    std::unique_ptr<Instance> Running;
    std::optional<SuspendedProcess> Suspended;
    Boundary At = 0;
    std::uint64_t NextEvent = 1;
    std::size_t Cleanups = 0;
    std::vector<HostEvent> Events;
    std::vector<Carrier> Carriers;
    std::vector<Carrier> Independent;
    std::vector<TraceEntry> History;
};
} // namespace pixel::formula::process::testing
