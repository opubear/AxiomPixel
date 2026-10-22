#include "ProcessFakeHost.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace pixel::formula::process::testing {
FakeHost::FakeHost(std::unique_ptr<Instance> instance) : Running(std::move(instance)) {
    if (!Running) throw std::invalid_argument("fake host requires an instance");
    Events.reserve(128);
    Carriers.reserve(32);
}
void FakeHost::Queue(Outcome outcome, RequestId request, Subscription subscription, Position position) {
    const auto &state = Running->State();
    const bool observable = request.Sequence != 0 ? request == state.LastRequest
        : state.WakeSubscription && subscription == *state.WakeSubscription;
    if (!observable) return;
    if (subscription.Sequence != 0 && std::any_of(Events.begin(), Events.end(), [&](const auto &event) {
            return event.WakeSubscription == subscription;
        })) return;
    if (Events.size() == 128 || NextEvent == std::numeric_limits<std::uint64_t>::max())
        throw std::logic_error("fake host event capacity exhausted");
    Events.push_back({NextEvent++, At, Running->State().Id, outcome, request, subscription, position});
}
void FakeHost::Apply(const StepResult &result) {
    for (const auto &request : result.Effects()) {
        const bool bolt = std::holds_alternative<Bolt>(request.Description);
        const auto outcome = RejectEffects || (bolt && Carriers.size() == 32) ? Outcome::Rejected : Outcome::Applied;
        History.push_back({At, request, outcome});
        Queue(outcome, request.Id);
        if (outcome == Outcome::Rejected) continue;
        if (std::holds_alternative<Dig>(request.Description)) {
            if (Inventory == std::numeric_limits<std::int64_t>::max()) throw std::logic_error("fake inventory overflow");
            ++Inventory;
        } else if (bolt) Carriers.push_back({request, std::get<Bolt>(request.Description).Lifetime});
    }
    if (result.End) {
        Carriers.clear();
        Events.clear();
        ++Cleanups;
    }
}
StepResult FakeHost::Advance() {
    // Freeze first; delivery and execution cannot observe effects applied below.
    const HostView view{At, Inventory};
    std::size_t delivered = 0;
    while (delivered < Events.size() && delivered < 32 && Events[delivered].CommittedAt < At) ++delivered;
    auto result = Running->Step(view, std::span<const HostEvent>(Events.data(), delivered));
    Events.erase(Events.begin(), Events.begin() + static_cast<std::ptrdiff_t>(delivered));
    // A new emit or await permanently retires the old addressable token. Facts
    // and carriers still exist; only notifications the program cannot read retire.
    const auto &state = Running->State();
    std::erase_if(Events, [&](const auto &event) {
        return event.Request.Sequence != 0 ? event.Request != state.LastRequest
            : !state.WakeSubscription || event.WakeSubscription != *state.WakeSubscription;
    });
    // Carriers age only on active boundaries, before newly emitted carriers join.
    for (auto carrier = Carriers.begin(); carrier != Carriers.end();) {
        if (--carrier->Remaining == 0) {
            Queue(Outcome::Expired, carrier->Request.Id);
            carrier = Carriers.erase(carrier);
        } else ++carrier;
    }
    Apply(result);
    if (At == std::numeric_limits<Boundary>::max()) throw std::logic_error("fake boundary exhausted");
    ++At;
    return result;
}
void FakeHost::Skip(std::uint64_t boundaries) {
    if (boundaries > std::numeric_limits<Boundary>::max() - At) throw std::invalid_argument("boundary overflow");
    At += boundaries;
}
void FakeHost::Hit(Position position) {
    for (const auto &carrier : Carriers) Queue(Outcome::Hit, carrier.Request.Id, {}, position);
    Carriers.clear();
}
void FakeHost::Wake(Outcome outcome) {
    if (outcome != Outcome::Wake && outcome != Outcome::Rejected && outcome != Outcome::Cancelled)
        throw std::invalid_argument("invalid wake terminal outcome");
    if (const auto subscription = Running->State().WakeSubscription)
        Queue(outcome, {}, *subscription);
}
void FakeHost::Cancel() { Apply(Running->Cancel()); }
} // namespace pixel::formula::process::testing
