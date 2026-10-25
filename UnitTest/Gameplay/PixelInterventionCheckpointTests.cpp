#include "../Support/TestAccess.h"
#include "../Support/CheckpointFiles.h"
#include "Dev/Debug/Garden/GardenAidProgram.h"
#include "Runtime/Gameplay/Persistence/GameplaySaveData.h"
#include "Runtime/Simulation/World/WorldStorageError.h"
#include "Dev/Debug/LLM/FakeModel.h"
#include "Runtime/Gameplay/WorldWill/WorldWill.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

using namespace pixel;
using namespace std::chrono_literals;
namespace {
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
struct EnvironmentVariable {
    std::string Name;
    std::optional<std::string> Previous;
    static void Set(const std::string &name, const char *value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value ? value : "");
#else
        if (value) setenv(name.c_str(), value, 1); else unsetenv(name.c_str());
#endif
    }
    EnvironmentVariable(std::string name, std::string value) : Name(std::move(name)) {
        if (const auto *old = std::getenv(Name.c_str())) Previous = old;
        Set(Name, value.c_str());
    }
    ~EnvironmentVariable() { Set(Name, Previous ? Previous->c_str() : nullptr); }
};
struct SaveRoot {
    std::filesystem::path Path = std::filesystem::temp_directory_path()
        / ("pixel-intervention-" + std::to_string(llm::Clock::now().time_since_epoch().count()));
    EnvironmentVariable Root{"PIXEL_SAVE_ROOT", Path.string()}, Window{"PIXEL_SIMULATION_WINDOW", "256"};
    ~SaveRoot() { std::filesystem::remove_all(Path); }
};
void Save(GameSession &session) {
    const auto result = session.Control(NativeCaller::Driver, api::SaveWorld{});
    if (!result.Succeeded()) throw std::runtime_error(result.Message);
}
void Step(GameSession &session, int count = 1) {
    for (int i = 0; i < count; ++i) {
        session.BeginFrame();
        Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "step refused");
        session.Tick(1.f / 60, {});
    }
}
api::GameplayObservation Snapshot(GameSession &session) {
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {464, 480, 96, 64});
    return view->Gameplay();
}
uint64_t SetupGarden(GameSession &session) {
    Check(session.Control(NativeCaller::Driver, api::CreateForceGarden{}).Succeeded(), "create garden");
    Check(session.Execute(NativeCaller::Input, api::ObserveForce{{GardenCrystalX, 508}}).Applied(), "discover crystal");
    for (int64_t x = 516; x <= 521; ++x)
        Check(session.Execute(NativeCaller::Input, api::MineRegion{{x, 514}, {}}).Applied(), "mine build cost");
    const auto placed = session.Execute(NativeCaller::Input, api::PlaceDevice{{GardenDeviceX, 510}});
    Check(placed.Applied(), "build device");
    Check(bool(session.ApplyProcess(NativeCaller::Driver, GardenAidProgram())), "apply aid program");
    return std::get<api::DeviceChanged>(placed.Result).Device.Ref.Id;
}
const std::string Prayer = "请帮助家园育圃。\nKeep the fixed home target.";
llm::Goal Goal(uint64_t device) { return {{{GardenDeviceX - 16, 494}, 34, 34}, {}, device, Prayer}; }
void Wait(will::WorldWill &decisions, size_t completed) {
    const auto until = llm::Clock::now() + 3s;
    while (decisions.Results().size() < completed && llm::Clock::now() < until) {
        decisions.Service();
        std::this_thread::sleep_for(1ms);
    }
    Check(decisions.Results().size() == completed, "worker completion timeout");
}
uint64_t Aid(GameSession &session, uint64_t device) {
    llm::FakeModel model;
    will::WorldWill decisions(session, model);
    Check(model.EnqueuePlan({0ms, true, true}), "held duplicate model plan");
    const auto requested = decisions.Request(Goal(device));
    Check(requested.Status == will::RequestStatus::Accepted, "prayer refused");
    Step(session, 3);
    Check(Snapshot(session).WorldWill.Records.back().State == api::DecisionState::Thinking, "request became a result");
    Check(model.Release(requested.Id), "release worker");
    session.Control(NativeCaller::Driver, api::SetPaused{false});
    Wait(decisions, 1);
    session.Control(NativeCaller::Driver, api::SetPaused{true});
    const auto &result = decisions.Results().back();
    Check(result.State == will::DecisionState::Applied && result.Action->Applied(), "aid failed");
    const auto state = Snapshot(session).WorldWill;
    Check(state.AidAccepted == 1 && state.Records.back().EvidenceSequence
        && state.Records.back().Facts.Count >= 2, "prayer lacks durable activation evidence");
    Check(decisions.Statistics().IgnoredReplies == 1, "duplicate reply replayed");
    return state.Records.back().InstanceId;
}

void AidSurvivesRestartAndProtection() {
    SaveRoot root;
    uint64_t device, instance, field, tick, history;
    uint32_t growth;
    uint64_t remaining, activeSteps, pendingWait;
    std::filesystem::path directory;
    api::RequestKey oldRequest;
    {
        GameSession session(true);
        device = SetupGarden(session);
        instance = Aid(session, device);
        Check(session.GetWorld().GetForce(GardenHomeX, 511).X == 3600, "no actual assistance field");
        Step(session, 7);
        Save(session);
        directory = session.GetSaveDirectory();
        const auto snapshot = Snapshot(session);
        growth = snapshot.Garden->State.HomeGrowth;
        tick = session.CurrentCommit().Tick;
        history = session.CaptureKnowledgeData().NextSequence;
        oldRequest = *snapshot.WorldWill.Records.back().Action;
        const auto sources = session.GetWorld().GetEnvironment().GetFieldSources().Sources();
        const auto source = std::find_if(sources.begin(), sources.end(), [&](const auto &s) { return s.Owner == instance; });
        Check(source != sources.end(), "missing intervention source");
        field = source->Handle.Id;
        remaining = source->Remaining;
        for (const auto &process : session.GetFormulaRuntime().InspectProcesses())
            if (process.State.Id == instance) { activeSteps = process.State.ActiveSteps; pendingWait = process.State.Remaining; }
    }
    {
        GameSession loaded(true);
        const auto snapshot = Snapshot(loaded);
        Check(loaded.CurrentCommit().Tick == tick && snapshot.Garden->State.HomeGrowth == growth,
            "load advanced garden or simulation");
        Check(snapshot.WorldWill.AidAccepted == 1 && snapshot.WorldWill.Records.size() == 1
            && snapshot.WorldWill.Records.front().State == api::DecisionState::Applied
            && snapshot.WorldWill.Records.front().Prayer == Prayer, "load lost, changed the original text or replayed prayer");
        Check(loaded.CaptureKnowledgeData().NextSequence == history
            && loaded.CaptureKnowledgeData().Discoveries.size() == 1, "knowledge not restored");
        Check(loaded.GetAuthority().Resources().Available == 3, "load minted influence slot");
        const auto sources = loaded.GetWorld().GetEnvironment().GetFieldSources().Sources();
        const auto source = std::find_if(sources.begin(), sources.end(), [&](const auto &s) { return s.Handle.Id == field; });
        Check(source != sources.end() && source->Owner == instance && source->Remaining == remaining,
            "load reinstalled source or renewed lifetime");
        for (const auto &process : loaded.GetFormulaRuntime().InspectProcesses())
            if (process.State.Id == instance)
                Check(process.State.ActiveSteps == activeSteps && process.State.Remaining == pendingWait,
                    "load restarted process continuation");
        Check(loaded.Submit(oldRequest.Caller, {oldRequest.WorldEpoch, oldRequest.Sequence,
            api::InterveneAtDevice{loaded.AppliedProgram(), loaded.GetDevices().Find(device)->Value.Ref,
                                  loaded.GetAuthority().Resources().Version}}).Rejection == api::Reason::CallerExpired,
            "old action session survived load");
        Step(loaded);
        Check(Snapshot(loaded).Garden->State.HomeGrowth == growth + 1, "resumed aid did not grow bed once");
        auto reference = loaded.GetDevices().Find(device)->Value.Ref;
        Check(loaded.Execute(NativeCaller::Input, api::SetDeviceProtection{reference, true}).Applied(), "install shield");
        Check(loaded.GetWorld().GetForce(GardenHomeX, 511).X == 3600, "disabled device shield unexpectedly active");
        Check(loaded.Execute(NativeCaller::Input, api::SetDeviceEnabled{reference, true}).Applied(), "enable shield");
        Check(loaded.GetWorld().GetForce(GardenHomeX, 511).X == 0 && loaded.GetAuthority().Resources().Available == 4,
            "protection failed to revoke restored source");
        Check(loaded.GetWorld().GetForce(GardenCrystalX, 508).X == 3600, "protection revoked natural crystal");
        const auto facts = loaded.CaptureKnowledgeData();
        Check(std::any_of(facts.History.begin(), facts.History.end(), [&](const auto &entry) {
            const auto *stopped = std::get_if<api::InterventionStopped>(&entry.Source.Payload);
            return stopped && stopped->Instance.InstanceId == instance && stopped->Reason == api::InterventionStopReason::ControlRevoked;
        }), "revocation not recorded");
        loaded.SetWorldWillCapabilities(NativeCaller::Driver, {true, false});
        Save(loaded);
    }
    GameSession autonomous(true);
    const auto state = Snapshot(autonomous);
    Check(state.WorldWill.AutonomyChosen && state.WorldWill.AidAccepted == 1 && state.Protections.size() == 1
        && state.Protections.front().Active && !state.AllowIntervention, "autonomy and capabilities not saved");
    Check(autonomous.GetWorld().GetForce(GardenHomeX, 511).X == 0, "revoked aid resurrected after load");
    const auto before = state.Garden->State.HomeGrowth;
    Step(autonomous, 3);
    Check(Snapshot(autonomous).Garden->State.HomeGrowth == before, "revoked descendants continued growth");
}

void PendingPrayerIsInterrupted() {
    SaveRoot root;
    uint64_t device;
    {
        GameSession session(true);
        device = SetupGarden(session);
        llm::FakeModel model;
        will::WorldWill decisions(session, model);
        Check(model.EnqueuePlan({60s, true}), "held request plan");
        Check(decisions.Request(Goal(device)).Status == will::RequestStatus::Accepted, "held request");
        Save(session); // No completed result is serialized as an action.
    }
    GameSession loaded(true);
    auto state = Snapshot(loaded).WorldWill;
    Check(state.Records.size() == 1 && state.Records.front().State == api::DecisionState::Interrupted
        && !state.Records.front().Action && state.AidAccepted == 0 && loaded.GetAuthority().Resources().Available == 4,
        "load resumed an in-flight inference");
    Save(loaded);
    GameSession again(true);
    Check(Snapshot(again).WorldWill.Records.front().State == api::DecisionState::Interrupted,
        "interrupted prayer cannot be resaved");
    Aid(again, device);
    Check(Snapshot(again).WorldWill.NextPrayer == 3, "durable prayer identity reused");
}

void ReadyPrayerRestoresWithoutCancellingFreshWork() {
    SaveRoot root;
    uint64_t device;
    api::PrayerRecord previousPrayer;
    {
        GameSession session(true);
        device = SetupGarden(session);
        llm::FakeModel model;
        will::WorldWill decisions(session, model);
        Check(decisions.Request(Goal(device)).Status == will::RequestStatus::Accepted, "ready save prayer request");
        const auto until = llm::Clock::now() + 3s;
        while (Snapshot(session).WorldWill.Records.back().State != api::DecisionState::Ready && llm::Clock::now() < until) {
            decisions.Service();
            std::this_thread::sleep_for(1ms);
        }
        const auto ready = Snapshot(session).WorldWill.Records.back();
        Check(ready.State == api::DecisionState::Ready && !ready.Action && ready.ResolvedAt == api::CommitStamp{},
            "paused model proposal was not published as unresolved ready state");
        previousPrayer = ready;
        Save(session);
    }
    {
        GameSession loaded(true);
        const auto interrupted = Snapshot(loaded).WorldWill.Records.back();
        Check(interrupted.State == api::DecisionState::Interrupted && interrupted.Prayer == Prayer && !interrupted.Action,
            "load replayed a ready proposal or lost original text");
        llm::FakeModel model;
        will::WorldWill decisions(loaded, model);
        Check(model.EnqueuePlan({0ms, true}), "fresh held worker plan");
        const auto fresh = decisions.Request(Goal(device));
        Check(fresh.Status == will::RequestStatus::Accepted, "fresh request after ready load");
        const auto currentPrayer = Snapshot(loaded).WorldWill.Records.back();
        Check(currentPrayer.Id > previousPrayer.Id && !decisions.CancelPrayer(previousPrayer.Id, previousPrayer.ObservedAt) && model.Release(fresh.Id),
            "cancelling a restored prayer stopped new work or durable identity was reused");
        loaded.Control(NativeCaller::Driver, api::SetPaused{false});
        Wait(decisions, 1);
        loaded.Control(NativeCaller::Driver, api::SetPaused{true});
        Check(Snapshot(loaded).WorldWill.AidAccepted == 1 && !decisions.CancelPrayer(currentPrayer.Id, currentPrayer.ObservedAt),
            "fresh post-load work failed or completed prayer stayed cancellable");
        Check(model.EnqueuePlan({0ms, true}), "cancel save worker plan");
        Check(decisions.Request(Goal(device)).Status == will::RequestStatus::Accepted, "cancel save request");
        const auto cancel = Snapshot(loaded).WorldWill.Records.back();
        Check(decisions.CancelPrayer(cancel.Id, cancel.ObservedAt), "cancel before save");
        Save(loaded);
    }
    GameSession cancelled(true);
    Check(Snapshot(cancelled).WorldWill.Records.back().State == api::DecisionState::Cancelled
        && Snapshot(cancelled).WorldWill.Records.back().Prayer == Prayer,
        "cancelled prayer or original text did not survive the next checkpoint");
}

void FrozenLeaseUsesRemainingGlobalTime() {
    SaveRoot root;
    uint64_t instance, expires, remaining, steps;
    {
        GameSession session(true);
        instance = Aid(session, SetupGarden(session));
        Step(session, 4);
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(3072, 508));
        testing::SessionAccess::World(session).LoadSimulationNow();
        session.Tick(0, {});
        Step(session, 5);
        const auto processes = session.GetFormulaRuntime().InspectProcesses();
        const auto process = std::find_if(processes.begin(), processes.end(), [&](const auto &p) { return p.State.Id == instance; });
        Check(process != processes.end() && process->Suspended, "intervention did not freeze");
        steps = process->State.ActiveSteps;
        const auto sources = session.GetWorld().GetEnvironment().GetFieldSources().Sources();
        const auto source = std::find_if(sources.begin(), sources.end(), [&](const auto &s) { return s.Owner == instance; });
        Check(source != sources.end() && source->Suspended, "field did not freeze");
        remaining = source->Remaining;
        Save(session);
        SessionStore store(root.Path);
        const auto checkpoint = store.ResolveCheckpoint(session.GetSaveDirectory());
        expires = DecodeGameplayState(store.ReadGameplayState(checkpoint)).Authority.Interventions.front().ExpiresAtTick;
    }
    {
        GameSession loaded(true);
        const auto sources = loaded.GetWorld().GetEnvironment().GetFieldSources().Sources();
        const auto source = std::find_if(sources.begin(), sources.end(), [&](const auto &s) { return s.Owner == instance; });
        Check(source != sources.end() && source->Suspended && source->Remaining == remaining,
            "frozen save lost source or consumed local lifetime");
        for (const auto &process : loaded.GetFormulaRuntime().InspectProcesses())
            if (process.State.Id == instance) Check(process.Suspended && process.State.ActiveSteps == steps, "frozen continuation advanced");
        // Only the final lease boundary matters here; unrelated terrain steps
        // are skipped explicitly, while expiry uses the production step path.
        testing::SessionAccess::RestoreClock(loaded, (expires - 1) * SimulationCoordinator::StepSeconds, 0);
        Step(loaded);
        Check(loaded.GetAuthority().Resources().Available == 4, "frozen lease was renewed on load");
        const auto stopped = loaded.CaptureKnowledgeData().History.back();
        const auto *reason = std::get_if<api::InterventionStopped>(&stopped.Source.Payload);
        Check(reason && reason->Instance.InstanceId == instance && reason->Reason == api::InterventionStopReason::LeaseExpired,
            "loaded frozen lease did not expire with an attributable fact");
    }
    // The untouched saved generation still contains the frozen intervention.
    GameSession revoked(true);
    Check(revoked.SetWorldWillCapabilities(NativeCaller::Driver, {true, false}).Succeeded(), "frozen revoke");
    Save(revoked);
    GameSession afterRevoke(true);
    const auto restoredProcesses = afterRevoke.GetFormulaRuntime().InspectProcesses();
    Check(afterRevoke.GetAuthority().Resources().Available == 4
        && std::none_of(restoredProcesses.begin(), restoredProcesses.end(),
                        [](const auto &process) { return process.Carrier == ProcessCarrierKind::Intervention; }),
        "frozen intervention resurrected after revocation checkpoint");
}

void CorruptCrossDomainStateIsRejected() {
    SaveRoot root;
    std::filesystem::path directory;
    {
        GameSession session(true);
        const auto device = SetupGarden(session);
        Aid(session, device);
        Step(session, 2);
        Save(session);
        directory = session.GetSaveDirectory();
    }
    SessionStore store(root.Path);
    const auto original = store.ResolveCheckpoint(directory);
    const auto metadata = store.ReadMetadata(original);
    const auto good = DecodeGameplayState(store.ReadGameplayState(original));
    const auto reject = [&](GameplaySaveData bad) {
        store.PublishCheckpoint(directory, original, metadata, EncodeGameplayState(bad));
        bool failed = false;
        try { GameSession loaded(true); }
        catch (const WorldStorageError &) { failed = true; }
        Check(failed, "corrupt cross-domain checkpoint was accepted");
    };
    auto bad = good;
    bad.Authority.Interventions.clear();
    bad.Authority.Influence.Available = 4;
    reject(bad); // Live process without a lease is never converted into a native source.
    bad = good;
    bad.Authority.Interventions.front().ExpiresAtTick = good.Tick;
    reject(bad);
    bad = good;
    bad.Authority.Interventions.front().Scope.Origin.X++;
    reject(bad);
    bad = good;
    bad.Authority.Influence.Available = 4;
    reject(bad);
    bad = good;
    bad.WorldWill.Records.front().EvidenceSequence++;
    reject(bad);
    bad = good;
    bad.Knowledge.LastTick = good.Tick + 1;
    reject(bad);
    store.PublishCheckpoint(directory, original, metadata, EncodeGameplayState(good));
    GameSession restored(true);
    Check(restored.GetAuthority().Resources().Available == 3, "valid checkpoint rejected after corruption cases");
}
} // namespace

int main() {
    try {
        AidSurvivesRestartAndProtection();
        PendingPrayerIsInterrupted();
        ReadyPrayerRestoresWithoutCancellingFreshWork();
        FrozenLeaseUsesRemainingGlobalTime();
        CorruptCrossDomainStateIsRejected();
        std::cout << "durable prayers, knowledge, interventions, autonomy and cross-domain validation passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
