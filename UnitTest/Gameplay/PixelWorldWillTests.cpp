#include "WorldWill/WorldWill.h"
#include "Gameplay/GameSession.h"
#include "Gameplay/Construction/DeviceCosts.h"
#include "../Support/TestAccess.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace pixel;
namespace {
using namespace std::chrono_literals;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

const char *Field = "process 1; emit field(cells(4), acceleration(100), acceleration(0), ticks(1200)); "
    "await result within ticks(1); wait ticks(1000);";
const llm::Goal CellGoal{{{92, 92}, 32, 32}, {100, 100}, {}};

void Apply(GameSession &session, std::string_view source = Field) {
    const auto program = formula::process::Compile("world-will-test", source);
    if (const auto *error = std::get_if<formula::process::Diagnostic>(&program)) throw std::runtime_error(error->Message);
    Check(bool(session.ApplyProcess(NativeCaller::Driver, std::get<formula::process::ProgramArtifact>(program))), "apply process");
}

// This adapter deliberately permits late and repeated replies after cancellation.
// The controller must reject them even when an external adapter fails to retract work.
class ManualModel final : public llm::LLM {
  public:
    bool Accept = true, Closed = false;
    std::vector<llm::ModelInput> Inputs;
    std::deque<llm::ModelReply> Replies;
    std::vector<llm::DecisionId> Cancelled;

    std::string_view DisplayName() const override { return "Manual test model"; }
    bool TryStart(llm::ModelInput input) override {
        if (!Accept || Closed) return false;
        Inputs.push_back(std::move(input));
        return true;
    }
    std::optional<llm::ModelReply> Poll() override {
        if (Replies.empty()) return {};
        auto reply = std::move(Replies.front());
        Replies.pop_front();
        return reply;
    }
    void Cancel(llm::DecisionId id) override { Cancelled.push_back(id); }
    void Close() override { Closed = true; }
    void Reply(llm::DecisionId id, llm::Proposal proposal = {llm::ProposalKind::AtCell, {100, 100}, 0}) {
        Replies.push_back({id, llm::ModelStatus::Proposed, proposal, "derived model explanation"});
    }
};

// Real gameplay and observation; only the host's service-boundary readiness is
// injected. AuthorityTests separately exercise a real streaming pending step.
class Host final : public api::WorldWillHost {
  public:
    GameSession Session;
    bool Pending = false, NotReady = false;
    size_t Submissions = 0;

    api::WorldWillHostStatus WorldWillStatus() const override {
        auto status = Session.WorldWillStatus();
        status.Pending = status.Pending || Pending;
        status.Ready = status.Ready && !NotReady;
        return status;
    }
    uint64_t BeginPrayer(const api::CommitStamp &stamp, api::Rect scope, std::string prayer) override {
        return Session.BeginPrayer(stamp, scope, std::move(prayer));
    }
    void ResolvePrayer(uint64_t id, api::DecisionState state, std::string explanation,
                       const std::optional<api::ActionReceipt> &receipt) override {
        Check(!Pending && !NotReady, "prayer lifecycle escaped the complete host boundary");
        Session.ResolvePrayer(id, state, std::move(explanation), receipt);
    }
    api::ActionReceipt Submit(api::CallerSessionId caller, const api::ActionRequest &request) override {
        ++Submissions;
        return Session.Submit(caller, request);
    }
    api::ObservationResult CaptureObservation(api::Rect scope) override { return Session.CaptureObservation(scope); }
    api::ObservationServiceStatus ObservationService() const override { return Session.ObservationService(); }
    api::ObservationAuthorization AuthorizeObservation(uint64_t subject, api::Rect scope) const override {
        return Session.AuthorizeObservation(subject, scope);
    }
    api::FactReadResult ReadFacts(uint64_t epoch, uint64_t after, uint64_t high, uint32_t limit) const override {
        return Session.ReadFacts(epoch, after, high, limit);
    }
};

struct Fixture {
    Host Source;
    ManualModel Model;
    will::WorldWill Will{Source, Model};
    llm::Clock::time_point Now = llm::Clock::time_point{} + 1s;

    Fixture() {
        Apply(Source.Session);
        Pause(false);
    }
    void Pause(bool paused) {
        Check(Source.Session.Control(NativeCaller::Driver, api::SetPaused{paused}).Succeeded(), "set paused");
    }
    llm::DecisionId Request(llm::Goal goal = CellGoal) {
        const auto result = Will.Request(goal, Now);
        Check(result.Status == will::RequestStatus::Accepted && result.Id, "decision request rejected");
        return result.Id;
    }
    void Service(std::chrono::milliseconds elapsed = 1ms) { Now += elapsed; Will.Service(Now); }
    const will::DecisionResult &Result(llm::DecisionId id) const {
        const auto &results = Will.Results();
        const auto found = std::find_if(results.begin(), results.end(), [&](const auto &result) { return result.Id == id; });
        Check(found != results.end(), "missing terminal decision");
        return *found;
    }
    DeviceRef PlaceDevice() {
        auto &inventory = testing::SessionAccess::Inventory(Source.Session);
        Check(inventory.Add(inventory.Reference(Source.Session.PlayerInventory()), ForceEmitterCost()) == items::Result::Applied,
            "device fixture stock");
        const auto receipt = Source.Session.Execute(NativeCaller::Input, api::PlaceDevice{{100, 100}});
        Check(receipt.Applied(), "device fixture placement");
        return std::get<api::DeviceChanged>(receipt.Result).Device.Ref;
    }
};

api::WorldWillMemory Prayers(GameSession &session) {
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {92, 92, 32, 32});
    return view->Gameplay().WorldWill;
}

void CheckApplied(const Fixture &fixture, llm::DecisionId id, uint64_t sequence = 1) {
    const auto &result = fixture.Result(id);
    Check(result.State == will::DecisionState::Applied && result.Action && result.Action->Applied(), "decision did not apply");
    const auto &receipt = *result.Action;
    Check(receipt.Key.Caller == fixture.Source.Session.WorldWillSession() && receipt.Key.Sequence == sequence,
        "controller used an obsolete or untrusted action identity");
    Check(receipt.Facts.Count >= 2, "activation omitted domain facts");
    const auto facts = fixture.Source.Session.ReadFacts(receipt.Facts.WorldEpoch, receipt.Facts.FirstSequence - 1,
        receipt.Facts.FirstSequence + receipt.Facts.Count - 1);
    bool activation = false, field = false;
    for (const auto &fact : facts.Facts) {
        if (std::holds_alternative<api::ProgramActivated>(fact.Payload)) {
            const auto *cause = std::get_if<api::RequestKey>(&fact.Cause);
            activation = cause && *cause == receipt.Key;
        }
        if (const auto *change = std::get_if<api::FieldSourceChanged>(&fact.Payload)) {
            const auto *cause = std::get_if<api::ProgramEffectCause>(&fact.Cause);
            field = change->Event == api::FieldSourceEvent::Installed && cause && cause->ActivationRequest == receipt.Key;
        }
    }
    Check(activation && field, "applied decision lost activation/effect causality");
    Check(fixture.Source.Session.GetWorld().GetForce(100, 100).X == 100, "receipt did not correspond to a real force field");
}

void DelayDoesNotStopSimulation() {
    Fixture f;
    const auto id = f.Request();
    const auto observed = f.Model.Inputs.front().ObservedAt;
    Check(f.Model.Inputs.front().Materials.size() == 32 * 32
        && f.Model.Inputs.front().OfferedProgram == f.Source.Session.AppliedProgram()
        && f.Model.Inputs.front().Resources == f.Source.Session.GetAuthority().Resources(), "model input lost owned observations or offers");
    for (int i = 0; i < 3; ++i) {
        f.Source.Session.BeginFrame();
        f.Source.Session.Tick(1.f / 60.f, {});
        f.Service();
    }
    Check(f.Source.Session.CurrentCommit().Tick > observed.Tick && f.Will.Statistics().Thinking == 1
        && f.Source.Submissions == 0, "simulation waited for model completion");
    f.Model.Reply(id);
    f.Service();
    CheckApplied(f, id);
    Check(f.Result(id).ObservedAt == observed && f.Model.Inputs.front().ObservedAt == observed,
        "commits mutated the owned model snapshot");
}

void ReturnOrderAndDuplicateDelivery() {
    Fixture f;
    const auto first = f.Request(), second = f.Request();
    Check(f.Will.Request(CellGoal, f.Now).Status == will::RequestStatus::Capacity, "unbounded in-flight decisions");
    f.Model.Reply(second);
    f.Model.Reply(second);
    f.Service();
    CheckApplied(f, second);
    Check(f.Source.Submissions == 1 && f.Will.Statistics().Thinking == 1, "duplicate completion executed twice");
    f.Model.Reply(first);
    f.Service();
    const auto &stale = f.Result(first);
    Check(stale.State == will::DecisionState::Rejected && stale.Action
        && stale.Action->Rejection == api::Reason::PreconditionFailed && stale.Action->Key.Sequence == 2,
        "late decision ignored current resources or reused an early sequence");
    const auto facts = f.Source.Session.FactHighWatermark();
    f.Model.Reply(second);
    f.Model.Reply(first);
    f.Service();
    Check(f.Source.Submissions == 2 && f.Source.Session.FactHighWatermark() == facts
        && f.Source.Session.GetAuthority().Resources().Available == 3
        && f.Will.Statistics().IgnoredReplies == 3, "retired replies replayed domain effects");
}

void EpochAndDeadline() {
    Fixture reset;
    const auto old = reset.Request();
    Check(reset.Source.Session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded(), "reset fixture");
    reset.Model.Reply(old);
    reset.Service();
    Check(reset.Result(old).State == will::DecisionState::StaleEpoch && reset.Source.Submissions == 0
        && reset.Source.Session.GetAuthority().Resources() == api::InterventionResources{}, "old epoch proposal executed");
    Check(reset.Model.Inputs.front().Materials.size() == 32 * 32, "reset invalidated model-owned data");

    Fixture timeout;
    const auto id = timeout.Request();
    timeout.Model.Reply(id);
    timeout.Service(5s);
    Check(timeout.Result(id).State == will::DecisionState::TimedOut && timeout.Source.Submissions == 0,
        "reply received at deadline executed");
    timeout.Model.Reply(id);
    timeout.Service();
    Check(timeout.Source.Submissions == 0 && timeout.Will.Statistics().IgnoredReplies == 1, "timed-out work revived");
}

void DeviceCurrentState() {
    for (const bool remove : {true, false}) {
        Fixture f;
        const auto device = f.PlaceDevice();
        const llm::Goal goal{{{84, 84}, 34, 34}, {}, device.Id};
        const auto id = f.Request(goal);
        Check(f.Model.Inputs.back().Devices.size() == 1 && f.Model.Inputs.back().Devices.front().Ref == device,
            "device proposal lacks captured version");
        if (remove) {
            Check(f.Source.Session.Execute(NativeCaller::Input, api::RemoveDevice{device}).Applied(), "remove target");
            Check(f.PlaceDevice().Id != device.Id, "replacement reused device identity");
        } else {
            Check(f.Source.Session.Execute(NativeCaller::Input, api::SetDeviceEnabled{device, true}).Applied(), "change target version");
        }
        f.Model.Reply(id, {llm::ProposalKind::AtDevice, {}, device.Id});
        f.Service();
        const auto &result = f.Result(id);
        Check(result.State == will::DecisionState::Rejected && result.Action
            && result.Action->Rejection == api::Reason::PreconditionFailed
            && f.Source.Session.GetAuthority().Resources().Available == 4, "changed device accepted a stale proposal");
    }

    Fixture valid;
    const auto device = valid.PlaceDevice();
    const auto id = valid.Request({{{84, 84}, 34, 34}, {}, device.Id});
    valid.Model.Reply(id, {llm::ProposalKind::AtDevice, {}, device.Id});
    valid.Service();
    CheckApplied(valid, id);
    const auto processes = valid.Source.Session.GetFormulaRuntime().InspectProcesses();
    const auto &carrier = processes.front().Configuration;
    Check(carrier.Origin.Cell() == CellCoord{101, 101} && carrier.Scope.Width == 34,
        "device completion did not use host-derived geometry");
}

void PreconditionsAreSpecific() {
    Fixture resource;
    const auto id = resource.Request();
    const auto status = resource.Source.Session.WorldWillStatus();
    const auto unrelated = resource.Source.Session.Submit(status.Caller, {status.Stamp.WorldEpoch, status.NextSequence,
        api::InstallIntervention{status.Program, {110, 110}, CellGoal.Scope, status.Resources.Version}});
    Check(unrelated.Applied(), "concurrent intervention fixture");
    resource.Model.Reply(id);
    resource.Service();
    Check(resource.Result(id).Action && resource.Result(id).Action->Rejection == api::Reason::PreconditionFailed
        && resource.Result(id).Action->Key.Sequence == 2, "resource version change escaped admission");

    Fixture program;
    const auto old = program.Request();
    Apply(program.Source.Session, "process 1; wait ticks(1000);");
    program.Model.Reply(old);
    program.Service();
    Check(program.Result(old).State == will::DecisionState::Rejected && program.Result(old).Action
        && program.Result(old).Action->Rejection == api::Reason::StaleProgram, "changed program accepted a stale offer");

    Fixture revision;
    const auto current = revision.Request();
    Check(revision.Source.Session.Execute(NativeCaller::Driver, api::SetWindBoundary{601}).Applied(), "unrelated revision fixture");
    Check(revision.Source.Session.CurrentCommit() != revision.Model.Inputs.front().ObservedAt, "fixture did not advance revision");
    revision.Model.Reply(current);
    revision.Service();
    CheckApplied(revision, current);
}

void AuthorityCannotReturnWithLateWork() {
    Fixture f;
    const auto id = f.Request();
    Check(f.Source.Session.SetWorldWillCapabilities(NativeCaller::Driver, {false, true}).Succeeded(), "revoke observations");
    f.Model.Reply(id);
    f.Service();
    Check(f.Result(id).State == will::DecisionState::ViewRevoked && f.Source.Submissions == 0, "revoked observation submitted an action");
    Check(f.Will.Request(CellGoal, f.Now).Status == will::RequestStatus::ObservationDenied, "revoked observer opened a new request");
    Check(f.Source.Session.SetWorldWillCapabilities(NativeCaller::Driver, {true, true}).Succeeded(), "restore observations");
    f.Model.Reply(id);
    f.Service();
    Check(f.Source.Submissions == 0, "restoring capability revived a revoked decision");
    const auto fresh = f.Request();
    f.Model.Reply(fresh);
    f.Service();
    CheckApplied(f, fresh);
}

void HoldUntilSafeBoundary() {
    Fixture f;
    const auto id = f.Request();
    f.Pause(true);
    f.Model.Reply(id);
    f.Service();
    Check(f.Will.Statistics().Ready == 1 && f.Source.Submissions == 0, "paused world accepted model action");
    f.Pause(false);
    f.Source.Pending = true;
    f.Service();
    Check(f.Will.Statistics().Ready == 1 && f.Source.Submissions == 0, "pending step accepted model action");
    Check(f.Will.Request(CellGoal, f.Now).Status == will::RequestStatus::NotReady, "pending world opened a decision");
    f.Source.Pending = false;
    f.Source.NotReady = true;
    f.Service();
    Check(f.Will.Statistics().Ready == 1 && f.Source.Submissions == 0, "unavailable host accepted model action");
    f.Source.NotReady = false;
    f.Service();
    CheckApplied(f, id);

    Fixture timeout;
    const auto waiting = timeout.Request();
    timeout.Pause(true);
    timeout.Model.Reply(waiting);
    timeout.Service(5s);
    Check(timeout.Result(waiting).State == will::DecisionState::TimedOut && timeout.Source.Submissions == 0,
        "paused ready proposal escaped deadline");

    for (const bool pendingStep : {true, false}) {
        Fixture blocked;
        const auto blockedId = blocked.Request();
        blocked.Source.Pending = pendingStep;
        blocked.Source.NotReady = !pendingStep;
        blocked.Service(5s);
        Check(blocked.Model.Cancelled == std::vector<llm::DecisionId>{blockedId}
            && blocked.Will.Results().empty(), "unsafe boundary delayed worker cancellation or published a terminal result");
        blocked.Model.Reply(blockedId);
        blocked.Service();
        Check(blocked.Source.Submissions == 0 && blocked.Model.Cancelled.size() == 1,
            "late reply escaped the blocked deadline or cancelled twice");
        blocked.Source.Pending = blocked.Source.NotReady = false;
        blocked.Service();
        Check(blocked.Result(blockedId).State == will::DecisionState::TimedOut
            && blocked.Source.Submissions == 0 && blocked.Model.Cancelled.size() == 1,
            "safe boundary revived an expired decision or cancelled twice");
    }
}

void MalformedAndUnavailableResults() {
    Fixture f;
    const auto watermark = f.Source.Session.FactHighWatermark();
    const std::array<llm::Proposal, 3> invalid{{
        {llm::ProposalKind::AtCell, {200, 200}, 0},
        {llm::ProposalKind::AtDevice, {}, 999},
        {static_cast<llm::ProposalKind>(99), {}, 0}}};
    for (const auto &proposal : invalid) {
        const auto id = f.Request();
        f.Model.Reply(id, proposal);
        f.Service();
        Check(f.Result(id).State == will::DecisionState::InvalidProposal && !f.Result(id).Action,
            "untrusted proposal escaped validation");
    }
    const auto longText = f.Request();
    f.Model.Reply(longText);
    f.Model.Replies.back().Explanation.assign(llm::ModelLimits::ExplanationBytes + 1, 'x');
    f.Service();
    Check(f.Result(longText).State == will::DecisionState::InvalidProposal && f.Result(longText).Explanation.empty(),
        "oversized explanation retained or submitted");
    const auto unavailable = f.Request();
    f.Model.Replies.push_back({unavailable, llm::ModelStatus::Unavailable, {}, "offline"});
    f.Service();
    Check(f.Result(unavailable).State == will::DecisionState::ModelUnavailable, "model failure became gameplay action");
    const auto invalidStatus = f.Request();
    f.Model.Replies.push_back({invalidStatus, llm::ModelStatus::Invalid, {}, "malformed provider reply"});
    f.Service();
    Check(f.Result(invalidStatus).State == will::DecisionState::InvalidProposal, "invalid provider reply became unavailable or applied");
    const auto none = f.Request();
    f.Model.Reply(none, {});
    f.Service();
    Check(f.Result(none).State == will::DecisionState::NoAction && !f.Result(none).Action,
        "explicit no-action result submitted a command");
    Check(f.Source.Submissions == 0 && f.Source.Session.FactHighWatermark() == watermark,
        "derived or invalid model response produced domain facts");
}

void RequestBoundsAndClose() {
    Fixture f;
    f.Model.Accept = false;
    Check(f.Will.Request(CellGoal, f.Now).Status == will::RequestStatus::ModelBusy
        && f.Will.Statistics().Thinking == 0, "adapter refusal occupied a decision slot");
    f.Model.Accept = true;
    auto large = CellGoal;
    large.Scope.Width = 65;
    Check(f.Will.Request(large, f.Now).Status == will::RequestStatus::InvalidGoal, "oversized observation accepted");
    auto outside = CellGoal;
    outside.Target = {200, 200};
    Check(f.Will.Request(outside, f.Now).Status == will::RequestStatus::InvalidGoal, "out-of-scope goal accepted");
    const auto first = f.Request(), second = f.Request();
    f.Will.Close();
    f.Will.Close();
    Check(f.Model.Closed && f.Model.Cancelled.size() == 2 && f.Result(first).State == will::DecisionState::Closed
        && f.Result(second).State == will::DecisionState::Closed, "close did not retire every decision exactly once");
    f.Model.Reply(first);
    f.Service();
    Check(f.Will.Request(CellGoal, f.Now).Status == will::RequestStatus::Closed
        && f.Source.Submissions == 0 && f.Will.Statistics().Thinking == 0, "closed controller resumed work");
}

void PlayerTextIsOwnedAndBounded() {
    Fixture f;
    Check(f.Will.ModelName() == "Manual test model", "adapter identity did not reach the controller");
    auto goal = CellGoal;
    goal.Prayer = "请照顾育圃 🌱\n";
    goal.Prayer.append(llm::ModelLimits::PrayerBytes - goal.Prayer.size(), 'x');
    const auto original = goal.Prayer;
    const auto id = f.Request(goal);
    goal.Prayer.assign("caller changed its text");
    Check(f.Model.Inputs.back().Intent.Prayer == original && Prayers(f.Source.Session).Records.back().Prayer == original,
        "player prayer was truncated or not owned by model input and durable record");
    const auto withText = f.Model.Inputs.back().RetainedBytes();
    auto withoutText = f.Model.Inputs.back();
    std::string{}.swap(withoutText.Intent.Prayer);
    Check(withText > withoutText.RetainedBytes(), "model memory accounting omitted owned player text");
    const auto next = Prayers(f.Source.Session).NextPrayer;
    for (const auto &invalid : {std::string(llm::ModelLimits::PrayerBytes + 1, 'x'), std::string("a\0b", 3),
        std::string("\x01", 1), std::string("\x7f", 1), std::string("\xc2\x85", 2),
        std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4),
        std::string("\xe4\xbd", 2)}) {
        goal.Prayer = invalid;
        Check(f.Will.Request(goal, f.Now).Status == will::RequestStatus::InvalidGoal,
            "unbounded or non-text prayer reached the model");
    }
    Check(f.Model.Inputs.size() == 1 && Prayers(f.Source.Session).NextPrayer == next,
        "rejected player text allocated a worker request or durable identity");
    f.Model.Reply(id);
    f.Pause(true);
    f.Service();
    const auto ready = Prayers(f.Source.Session).Records.back();
    Check(ready.State == api::DecisionState::Ready && ready.Prayer == original && !ready.Action
        && ready.Facts.Count == 0 && ready.ResolvedAt == api::CommitStamp{} && !ready.Explanation.empty(),
        "ready proposal became action evidence or lost its player source");
    const auto revision = f.Source.Session.CurrentCommit().Revision;
    f.Service();
    Check(f.Source.Session.CurrentCommit().Revision == revision, "ready publication repeated each service frame");
    f.Pause(false);
    f.Service();
    CheckApplied(f, id);
    Check(Prayers(f.Source.Session).Records.back().Prayer == original, "terminal result replaced original player text");
}

void ExplicitCancellationWaitsForCommitAndRejectsLateReplies() {
    for (const bool pendingStep : {true, false}) {
        Fixture f;
        const auto id = f.Request();
        if (pendingStep) {
            f.Pause(true);
            f.Model.Reply(id);
            f.Service();
        }
        const auto before = Prayers(f.Source.Session).Records.back();
        f.Source.Pending = pendingStep;
        f.Source.NotReady = !pendingStep;
        Check(f.Will.CancelPrayer(before.Id, before.ObservedAt) && !f.Will.CancelPrayer(before.Id, before.ObservedAt), "cancel was not admitted exactly once");
        Check(f.Model.Cancelled == std::vector<llm::DecisionId>{id} && f.Will.Results().empty()
            && Prayers(f.Source.Session).Records.back().State == before.State,
            "cancellation waited for the worker or changed an incomplete game commit");
        f.Model.Reply(id);
        f.Service();
        Check(f.Will.Statistics().IgnoredReplies == 1 && f.Source.Submissions == 0 && f.Will.Results().empty(),
            "cancelled worker's late response was retained or submitted");
        f.Source.Pending = f.Source.NotReady = false;
        f.Service();
        const auto cancelled = Prayers(f.Source.Session).Records.back();
        Check(f.Result(id).State == api::DecisionState::Cancelled && cancelled.State == api::DecisionState::Cancelled
            && cancelled.ResolvedAt != api::CommitStamp{} && !cancelled.Action && !cancelled.Facts.Count
            && f.Model.Cancelled.size() == 1 && f.Source.Submissions == 0,
            "safe boundary did not publish one cancellation without effects");
        Check(!f.Will.CancelPrayer(before.Id, before.ObservedAt) && !f.Will.CancelPrayer(0, before.ObservedAt), "terminal or empty prayer remained cancellable");
    }
    Fixture current;
    const auto id = current.Request();
    const auto prayer = Prayers(current.Source.Session).Records.back();
    Check(current.Will.CancelPrayer(prayer.Id, prayer.ObservedAt) && current.Result(id).State == api::DecisionState::Cancelled,
        "complete boundary deferred an explicit cancellation");
    current.Model.Reply(id);
    current.Service();
    Check(current.Source.Submissions == 0, "completed cancellation accepted a late action");

    Fixture changed;
    const auto old = changed.Request();
    const auto oldPrayer = Prayers(changed.Source.Session).Records.back();
    Check(changed.Source.Session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded(), "cancel reset fixture");
    Check(!changed.Will.CancelPrayer(oldPrayer.Id, oldPrayer.ObservedAt), "old epoch prayer accepted current-world cancellation");
    Apply(changed.Source.Session);
    const auto fresh = changed.Request();
    const auto freshPrayer = Prayers(changed.Source.Session).Records.back();
    Check(freshPrayer.Id == oldPrayer.Id && freshPrayer.ObservedAt.WorldEpoch != oldPrayer.ObservedAt.WorldEpoch,
        "new-world cancellation fixture did not reuse the durable prayer number");
    Check(!changed.Will.CancelPrayer(oldPrayer.Id, oldPrayer.ObservedAt)
        && std::find(changed.Model.Cancelled.begin(), changed.Model.Cancelled.end(), fresh) == changed.Model.Cancelled.end(),
        "old UI prayer reference cancelled same-number work in the new world");
    changed.Service();
    Check(changed.Result(old).State == will::DecisionState::StaleEpoch
        && Prayers(changed.Source.Session).Records.back().State == api::DecisionState::Thinking,
        "stale cancellation resolved a prayer in the new world");
    changed.Pause(false);
    changed.Model.Reply(fresh);
    changed.Service();
    CheckApplied(changed, fresh);
}

void TerminalHistoryIsBounded() {
    Fixture f;
    llm::DecisionId first = 0, last = 0;
    for (size_t i = 0; i < llm::ModelLimits::Results + 3; ++i) {
        last = f.Request();
        if (!first) first = last;
        f.Model.Reply(last, {});
        f.Service();
    }
    const auto &results = f.Will.Results();
    Check(results.size() == llm::ModelLimits::Results && results.front().Id != first && results.back().Id == last
        && f.Will.Statistics().Completed == llm::ModelLimits::Results, "terminal history grew without a bound");
    f.Model.Reply(first);
    f.Service();
    Check(f.Source.Submissions == 0 && f.Will.Statistics().IgnoredReplies == 1, "eviction made a retired ID executable again");
}
} // namespace

int main() {
    try {
        DelayDoesNotStopSimulation(); ReturnOrderAndDuplicateDelivery(); EpochAndDeadline(); DeviceCurrentState();
        PreconditionsAreSpecific(); AuthorityCannotReturnWithLateWork(); HoldUntilSafeBoundary();
        MalformedAndUnavailableResults(); RequestBoundsAndClose(); TerminalHistoryIsBounded();
        PlayerTextIsOwnedAndBounded(); ExplicitCancellationWaitsForCommitAndRejectsLateReplies();
        std::cout << "world will delay, ordering, current-state admission, revocation, deadlines and lifecycle passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
