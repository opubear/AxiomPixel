#include "WorldWill.h"
#include "Runtime/Gameplay/LLMView/QueryProjection.h"
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace pixel::will {
namespace {
llm::DecisionId NewDecision() {
    static std::atomic<uint64_t> next{1};
    const auto id = next.fetch_add(1);
    if (!id || id == UINT64_MAX) std::terminate();
    return id;
}
} // namespace

const char *DecisionStateName(DecisionState state) {
    switch (state) {
#define WILL_STATE(name) case DecisionState::name: return #name
        WILL_STATE(Thinking); WILL_STATE(Ready); WILL_STATE(Applied); WILL_STATE(Rejected);
        WILL_STATE(NoAction); WILL_STATE(TimedOut); WILL_STATE(StaleEpoch); WILL_STATE(ViewRevoked);
        WILL_STATE(ModelUnavailable); WILL_STATE(InvalidProposal); WILL_STATE(Closed);
        WILL_STATE(Interrupted); WILL_STATE(Cancelled);
#undef WILL_STATE
    }
    throw std::logic_error("unknown decision state");
}

WorldWill::WorldWill(api::WorldWillHost &host, llm::LLM &model, std::chrono::milliseconds timeout)
    : Host(host), Model(model), Views(host), Timeout(timeout) {
    if (timeout.count() <= 0 || timeout > std::chrono::seconds(60)) throw std::invalid_argument("decision timeout outside [1ms,60s]");

    Active.reserve(llm::ModelLimits::InFlight);
}

WorldWill::~WorldWill() { Close(); }

void WorldWill::CheckTime(llm::Clock::time_point now) {
    if (now < LastService || now > llm::Clock::time_point::max() - Timeout)
        throw std::invalid_argument("world will service clock must be monotonic and bounded");
    LastService = now;
}

void WorldWill::Finish(size_t index, DecisionState state, std::optional<api::ActionReceipt> receipt) {
    auto &pending = Active.at(index);
    DecisionResult result{pending.Id, state, pending.ObservedAt, std::move(receipt), {}};
    if (pending.Reply && pending.Reply->Explanation.size() <= llm::ModelLimits::ExplanationBytes)
        result.Explanation = std::move(pending.Reply->Explanation);
    const auto host = Host.WorldWillStatus();
    if (pending.PrayerId && host.Stamp.WorldEpoch == pending.ObservedAt.WorldEpoch && host.Ready && !host.Pending)
        Host.ResolvePrayer(pending.PrayerId, state, result.Explanation, result.Action);
    if (!pending.ModelCancelled) Model.Cancel(pending.Id);
    if (Completed.size() == llm::ModelLimits::Results) Completed.pop_front();
    Completed.push_back(std::move(result));
    Active.erase(Active.begin() + index);
}

RequestResult WorldWill::Observe(llm::Goal goal, const api::WorldWillHostStatus &status, llm::ModelInput &input) {
    llm::Policy policy;
    policy.AuthoritySubject = status.Caller;
    policy.Region = goal.Scope;
    policy.Topics[size_t(api::Topic::Materials)] = true;
    policy.Topics[size_t(api::Topic::Devices)] = true;
    if (Observer && BoundCaller != status.Caller) { Views.RemoveObserver(Observer); Observer = 0; }
    if (!Observer) {
        Observer = Views.RegisterObserver(policy);
        BoundCaller = status.Caller;
    } else Views.SetPolicy(Observer, policy);
    if (!Observer) throw std::logic_error("private world will observer capacity exhausted");

    const auto opened = Views.OpenView(Observer, goal.Scope);
    if (!opened.View) return {RequestStatus::ObservationDenied, 0, opened.Result};

    input.ObservedAt = opened.View->Stamp;
    const auto view = opened.View->Id;
    for (const auto topic : {api::Topic::Materials, api::Topic::Devices}) {
        llm::CursorId cursor = 0;
        do {
            const auto prepared = Views.Query(Observer, view, {topic, goal.Scope}, cursor);
            if (prepared.Result != llm::Status::Ok) {
                Views.CloseView(Observer, view);
                return {RequestStatus::ObservationDenied, 0, prepared.Result};
            }
            auto delivered = Views.Deliver(Observer, prepared.Delivery);
            if (delivered.Result != llm::Status::Ok || !delivered.Page) {
                Views.CloseView(Observer, view);
                return {RequestStatus::ObservationDenied, 0, delivered.Result};
            }
            const auto &page = *delivered.Page;
            input.Materials.insert(input.Materials.end(), page.Cells.begin(), page.Cells.end());
            input.Devices.insert(input.Devices.end(), page.Devices.begin(), page.Devices.end());
            cursor = page.NextCursor;
        } while (cursor);
    }
    Views.CloseView(Observer, view);
    if (input.Materials.size() > llm::ModelLimits::Cells || input.Devices.size() > llm::ModelLimits::Devices
        || input.RetainedBytes() > llm::ModelLimits::InputBytes)
        throw std::logic_error("bounded observation exceeded model input contract");
    return {RequestStatus::Accepted};
}

RequestResult WorldWill::Request(llm::Goal goal, llm::Clock::time_point now) {
    CheckTime(now);
    if (Stopped) return {RequestStatus::Closed};
    if (Active.size() == llm::ModelLimits::InFlight) return {RequestStatus::Capacity};
    const auto status = Host.WorldWillStatus();
    if (!status.Ready || status.Pending) return {RequestStatus::NotReady};
    if (!llm::ValidRect(goal.Scope) || goal.Scope.Width > 64 || goal.Scope.Height > 64
        || (!goal.Device && !llm::Contains(goal.Scope, {goal.Target, 1, 1}))
        || (goal.Device && !*goal.Device) || !api::ValidPrayerText(goal.Prayer)) return {RequestStatus::InvalidGoal};

    llm::ModelInput input;
    input.Intent = goal;
    input.OfferedProgram = status.Program;
    input.Resources = status.Resources;
    const auto observation = Observe(goal, status, input);
    if (observation.Status != RequestStatus::Accepted) return observation;

    const auto authorization = Host.AuthorizeObservation(status.Caller, goal.Scope);
    if (!authorization.Allowed) return {RequestStatus::ObservationDenied, 0, llm::Status::ViewRevoked};

    input.Id = NewDecision();
    Pending pending{input.Id, input.ObservedAt, status.Caller, authorization.Version, goal,
        status.Program, status.Resources, input.Devices, now + Timeout, {}};
    if (!Model.TryStart(std::move(input))) return {RequestStatus::ModelBusy};
    pending.PrayerId = Host.BeginPrayer(pending.ObservedAt, goal.Scope, goal.Prayer);
    Active.push_back(std::move(pending));
    return {RequestStatus::Accepted, Active.back().Id};
}

bool WorldWill::CancelPrayer(uint64_t durablePrayerId, const api::CommitStamp &observed) {
    if (Stopped || !durablePrayerId) return false;
    const auto status = Host.WorldWillStatus();
    const auto found = std::find_if(Active.begin(), Active.end(), [&](const auto &pending) {
        return pending.PrayerId == durablePrayerId && pending.ObservedAt == observed
            && pending.ObservedAt.WorldEpoch == status.Stamp.WorldEpoch
            && pending.ObservedAt.WorldId == status.Stamp.WorldId && pending.Caller == status.Caller;
    });
    if (found == Active.end() || found->ModelCancelled) return false;
    found->CancelRequested = true;
    found->ModelCancelled = true;
    Model.Cancel(found->Id);
    if (status.Ready && !status.Pending) Finish(size_t(found - Active.begin()), DecisionState::Cancelled);
    return true;
}

std::optional<api::Action> WorldWill::ActionFor(const Pending &pending) const {
    const auto &proposal = pending.Reply->Choice;
    if (proposal.Kind == llm::ProposalKind::AtCell) {
        if (!llm::Contains(pending.Intent.Scope, {proposal.Target, 1, 1})) return {};
        return api::InstallIntervention{pending.Program, proposal.Target, pending.Intent.Scope, pending.Resources.Version};
    }

    if (proposal.Kind == llm::ProposalKind::AtDevice) {
        const auto found = std::find_if(pending.Devices.begin(), pending.Devices.end(),
            [&](const auto &device) { return device.Ref.Id == proposal.DeviceId; });
        if (found == pending.Devices.end()) return {};
        const api::Rect footprint{{found->Anchor.X - 16, found->Anchor.Y - 16}, 34, 34};
        if (!llm::ValidRect(footprint) || !llm::Contains(pending.Intent.Scope, footprint)) return {};
        return api::InterveneAtDevice{pending.Program, found->Ref, pending.Resources.Version};
    }
    return {};
}

void WorldWill::Service(llm::Clock::time_point now) {
    CheckTime(now);
    if (Stopped) return;
    Views.AdvanceServiceFrame();

    // Drain at most a fixed number, even if an adapter misbehaves. Duplicates
    // never get new action sequences; retired IDs have no authority to submit.
    for (size_t i = 0; i < 8; ++i) {
        auto reply = Model.Poll();
        if (!reply) break;
        auto found = std::find_if(Active.begin(), Active.end(), [&](const auto &pending) { return pending.Id == reply->Id; });
        if (found == Active.end() || found->Reply || found->ModelCancelled) { ++IgnoredReplies; continue; }
        found->Reply = std::move(reply);
    }

    for (size_t i = 0; i < Active.size();) {
        auto &pending = Active[i];
        const auto status = Host.WorldWillStatus();
        const auto authorization = Host.AuthorizeObservation(pending.Caller, pending.Intent.Scope);
        if (status.Stamp.WorldEpoch != pending.ObservedAt.WorldEpoch || status.Caller != pending.Caller) {
            Finish(i, DecisionState::StaleEpoch);
        } else if (!status.Ready || status.Pending) {
            // Keep the terminal result pending until it can share a complete
            // game commit, but never extend the worker's wall-clock deadline.
            if (now >= pending.Deadline && !pending.ModelCancelled) {
                Model.Cancel(pending.Id);
                pending.ModelCancelled = true;
            }
            ++i;
        } else if (pending.CancelRequested) {
            Finish(i, DecisionState::Cancelled);
        } else if (!authorization.Allowed || authorization.Version != pending.AuthorizationVersion) {
            Finish(i, DecisionState::ViewRevoked);
        } else if (now >= pending.Deadline) {
            Finish(i, DecisionState::TimedOut);
        } else if (!pending.Reply) { ++i; }
        else if (pending.Reply->Explanation.size() > llm::ModelLimits::ExplanationBytes) {
            Finish(i, DecisionState::InvalidProposal);
        } else if (pending.Reply->Status == llm::ModelStatus::Unavailable) {
            Finish(i, DecisionState::ModelUnavailable);
        } else if (pending.Reply->Status != llm::ModelStatus::Proposed) {
            Finish(i, DecisionState::InvalidProposal);
        } else if (pending.Reply->Choice.Kind == llm::ProposalKind::None) {
            Finish(i, DecisionState::NoAction);
        } else if (const auto action = ActionFor(pending)) {
            if (status.Paused) {
                if (pending.PrayerId && !pending.PrayerReady) {
                    Host.ResolvePrayer(pending.PrayerId, DecisionState::Ready, pending.Reply->Explanation, {});
                    pending.PrayerReady = true;
                }
                ++i;
                continue;
            }
            // No equality check against the global revision: the action owns
            // program/device/resource preconditions and rechecks current control.
            auto receipt = Host.Submit(pending.Caller, {pending.ObservedAt.WorldEpoch, status.NextSequence, *action});
            const auto state = receipt.Applied() ? DecisionState::Applied : DecisionState::Rejected;
            Finish(i, state, std::move(receipt));
        } else Finish(i, DecisionState::InvalidProposal);
    }
}

void WorldWill::Close() {
    if (Stopped) return;
    Stopped = true;
    while (!Active.empty()) Finish(0, Active.front().CancelRequested ? DecisionState::Cancelled : DecisionState::Closed);
    if (Observer) { Views.RemoveObserver(Observer); Observer = 0; }
    Model.Close();
}

Metrics WorldWill::Statistics() const {
    Metrics result;
    for (const auto &pending : Active) (pending.Reply ? result.Ready : result.Thinking)++;
    result.Completed = Completed.size();
    result.IgnoredReplies = IgnoredReplies;
    return result;
}
} // namespace pixel::will
