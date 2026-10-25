#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Dev/Debug/Garden/ForceGarden.h"
#include "Runtime/Simulation/World/WorldCoordinates.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
using namespace pixel;
using namespace pixel::knowledge;

void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

class TemporarySaveRoot {
  public:
    std::filesystem::path Path = std::filesystem::temp_directory_path()
        / ("pixel-knowledge-pending-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    TemporarySaveRoot() {
        std::filesystem::remove_all(Path);
        if (const auto *value = std::getenv("PIXEL_SAVE_ROOT")) PreviousRoot = value;
        if (const auto *value = std::getenv("PIXEL_SIMULATION_WINDOW")) PreviousWindow = value;
#if defined(_WIN32)
        _putenv_s("PIXEL_SAVE_ROOT", Path.string().c_str());
        _putenv_s("PIXEL_SIMULATION_WINDOW", "256");
#else
        setenv("PIXEL_SAVE_ROOT", Path.string().c_str(), 1);
        setenv("PIXEL_SIMULATION_WINDOW", "256", 1);
#endif
    }

    ~TemporarySaveRoot() {
#if defined(_WIN32)
        _putenv_s("PIXEL_SAVE_ROOT", PreviousRoot ? PreviousRoot->c_str() : "");
        _putenv_s("PIXEL_SIMULATION_WINDOW", PreviousWindow ? PreviousWindow->c_str() : "");
#else
        if (PreviousRoot) setenv("PIXEL_SAVE_ROOT", PreviousRoot->c_str(), 1);
        else unsetenv("PIXEL_SAVE_ROOT");
        if (PreviousWindow) setenv("PIXEL_SIMULATION_WINDOW", PreviousWindow->c_str(), 1);
        else unsetenv("PIXEL_SIMULATION_WINDOW");
#endif
        std::filesystem::remove_all(Path);
    }

  private:
    std::optional<std::string> PreviousRoot, PreviousWindow;
};

api::DomainFact Fact(uint64_t sourceSequence, uint64_t epoch, uint64_t revision,
                     api::FactPayload payload, std::string worldId = "knowledge-world") {
    api::DomainFact fact;
    fact.Id = {epoch, sourceSequence};
    fact.CommittedAt = {std::move(worldId), epoch, sourceSequence, revision};
    fact.Cause = api::RequestKey{epoch, 1, sourceSequence};
    fact.Payload = std::move(payload);
    return fact;
}

api::DomainFact Removed(uint64_t sequence, uint64_t epoch = 7) {
    return Fact(sequence, epoch, sequence, api::MaterialRemoved{{int64_t(sequence), 4}, 1, 0});
}

void ExpectRestoreFailure(const SaveData &data) {
    Journal journal;
    bool rejected = false;
    try { journal.Restore(data); }
    catch (const std::invalid_argument &) { rejected = true; }
    Check(rejected, "knowledge restore accepted malformed data");
}

bool SameObservationKnowledge(const api::ObservationSnapshot &left, const api::ObservationSnapshot &right) {
    if (left.Stamp != right.Stamp || left.Knowledge.Stamp != right.Knowledge.Stamp
        || left.Knowledge.FirstSequence != right.Knowledge.FirstSequence
        || left.Knowledge.HighWatermark != right.Knowledge.HighWatermark
        || left.Knowledge.History.size() != right.Knowledge.History.size()
        || left.Knowledge.Discoveries.size() != right.Knowledge.Discoveries.size()) return false;
    for (size_t index = 0; index < left.Knowledge.History.size(); ++index) {
        const auto &a = left.Knowledge.History[index];
        const auto &b = right.Knowledge.History[index];
        if (a.Sequence != b.Sequence || a.Source.Id != b.Source.Id
            || a.Source.CommittedAt != b.Source.CommittedAt) return false;
        const auto *forceA = std::get_if<api::ForceObserved>(&a.Source.Payload);
        const auto *forceB = std::get_if<api::ForceObserved>(&b.Source.Payload);
        if (bool(forceA) != bool(forceB)) return false;
        if (forceA && (forceA->ActorId != forceB->ActorId || forceA->Location != forceB->Location
            || forceA->AccelerationCellsPerSecondSquared != forceB->AccelerationCellsPerSecondSquared)) return false;
    }
    for (size_t index = 0; index < left.Knowledge.Discoveries.size(); ++index) {
        const auto &a = left.Knowledge.Discoveries[index];
        const auto &b = right.Knowledge.Discoveries[index];
        if (a.OwnerActor != b.OwnerActor || a.Location != b.Location
            || a.EvidenceSequence != b.EvidenceSequence || a.Observed != b.Observed
            || a.Analyzed != b.Analyzed || a.CanCopy != b.CanCopy || a.CanEdit != b.CanEdit) return false;
    }
    return true;
}

void JournalSelectionAndOrdering() {
    Journal journal;
    auto placed = Fact(1, 7, 1, api::MaterialPlaced{{1, 1}, 0, 1});
    auto stateChanged = Fact(2, 7, 2, api::DeviceChanged{{}, api::DeviceEvent::StateChanged});
    journal.Record(placed);
    journal.Record(stateChanged);
    Check(journal.Export().History.empty(), "routine placement or device state entered history");

    auto mined = Removed(3);
    journal.Record(mined);
    journal.Record(mined);
    journal.Record(Fact(4, 7, 4, api::DeviceChanged{{}, api::DeviceEvent::Placed}));
    journal.Record(Fact(5, 7, 5, api::ForceObserved{11, {8, 9}, {3600, 0}}));
    const auto save = journal.Export();
    Check(save.History.size() == 3
        && std::holds_alternative<api::MaterialRemoved>(save.History[0].Source.Payload)
        && std::get<api::DeviceChanged>(save.History[1].Source.Payload).Event == api::DeviceEvent::Placed
        && std::holds_alternative<api::ForceObserved>(save.History[2].Source.Payload),
        "important mining, device and force facts were not selected in source order");
    Check(save.Discoveries.size() == 1 && save.Discoveries[0].Observed
        && !save.Discoveries[0].Analyzed && !save.Discoveries[0].CanCopy && !save.Discoveries[0].CanEdit,
        "an observation granted analysis, copying or editing permission");

    Journal reordered;
    reordered.Record(Removed(1));
    bool failed = false;
    try { reordered.Record(Removed(3)); }
    catch (const std::logic_error &) { failed = true; }
    Check(failed, "journal did not fail when its source consumer missed a fact");
}

void BoundedHistoryAndDiscoveries() {
    Journal journal;
    for (uint64_t sequence = 1; sequence <= HistoryCapacity + 2; ++sequence)
        journal.Record(Removed(sequence));
    const api::CommitStamp stamp{"knowledge-world", 7, 130, 130};
    const auto snapshot = journal.Capture(stamp);
    Check(snapshot.History.size() == HistoryCapacity && snapshot.FirstSequence == 3
        && snapshot.HighWatermark == 130 && snapshot.History.front().Sequence == 3,
        "history did not retain the bounded contiguous suffix");
    Check(ReadHistory(snapshot, 0, 16).Status == ReadStatus::Gap,
        "history reader did not report an evicted range gap");
    const auto page = ReadHistory(snapshot, 2, 16);
    Check(page.Status == ReadStatus::Available && page.Records.size() == 16
        && page.Records.front().Sequence == 3 && page.NextSequence == 18 && page.HasMore,
        "history page did not honor its cursor and limit");
    Check(ReadHistory(snapshot, 0, HistoryCapacity + 1).Status == ReadStatus::LimitExceeded,
        "history reader accepted an oversized page");
    const auto retained = snapshot.RetainedBytes();
    journal.Record(Removed(HistoryCapacity + 3));
    Check(snapshot.History.size() == HistoryCapacity && snapshot.HighWatermark == 130
        && snapshot.RetainedBytes() == retained,
        "captured history snapshot changed after the journal advanced");

    Journal discoveries;
    for (uint64_t index = 0; index < DiscoveryCapacity; ++index) {
        const auto location = api::Cell{int64_t(index), 10};
        Check(discoveries.CanDiscover(99, location), "discovery slot was not available");
        discoveries.Record(Fact(index + 1, 9, index + 1, api::ForceObserved{99, location, {3600, 0}}));
    }
    Check(!discoveries.CanDiscover(99, {DiscoveryCapacity, 10}), "discovery cap was not enforced");
    const auto beforeDuplicate = discoveries.Export();
    const auto repeated = Fact(DiscoveryCapacity + 1, 9, DiscoveryCapacity + 1,
        api::ForceObserved{99, {0, 10}, {3600, 0}});
    Check(discoveries.CanDiscover(99, {0, 10}), "a known location was refused at capacity");
    discoveries.Record(repeated);
    const auto afterDuplicate = discoveries.Export();
    Check(afterDuplicate.History.size() == beforeDuplicate.History.size()
        && afterDuplicate.Discoveries.size() == DiscoveryCapacity,
        "repeated discovery at capacity created a duplicate history item");
}

void CheckpointContract() {
    Journal source;
    source.Record(Removed(1, 41));
    source.Record(Fact(2, 41, 2, api::ForceObserved{77, {5, 6}, {3600, 0}}));
    auto exported = source.Export();
    Journal restored;
    restored.Restore(exported);
    exported.History.clear();
    exported.Discoveries.clear();
    Check(restored.Export().History.size() == 2 && restored.Export().Discoveries.size() == 1,
        "mutating an exported DTO wrote through into journal state");

    auto invalid = restored.Export();
    invalid.Version++;
    ExpectRestoreFailure(invalid);
    invalid = restored.Export();
    invalid.WorldId = "other-world";
    ExpectRestoreFailure(invalid);
    invalid = restored.Export();
    invalid.NextSequence++;
    ExpectRestoreFailure(invalid);
    invalid = restored.Export();
    invalid.Discoveries[0].CanCopy = true;
    ExpectRestoreFailure(invalid);
    invalid = restored.Export();
    invalid.History[0].Sequence++;
    ExpectRestoreFailure(invalid);
    invalid = restored.Export();
    invalid.LastRevision = 1;
    ExpectRestoreFailure(invalid);
    invalid = restored.Export();
    invalid.LastSource.Sequence = 1;
    ExpectRestoreFailure(invalid);
    invalid = restored.Export();
    invalid.History[1].Source.Id.Sequence = invalid.History[0].Source.Id.Sequence;
    ExpectRestoreFailure(invalid);
    invalid = restored.Export();
    invalid.History[0].Source.Id.Sequence = 2;
    invalid.History[1].Source.Id.Sequence = 1;
    ExpectRestoreFailure(invalid);
    invalid = restored.Export();
    invalid.LastTick = 1;
    ExpectRestoreFailure(invalid);

    const auto worldId = restored.Export().WorldId;
    restored.BeginRuntime(41);
    restored.Record(Fact(1, 41, 3, api::MaterialRemoved{{12, 4}, 1, 0}, worldId));
    const auto continued = restored.Export();
    Journal runtimeRestored;
    runtimeRestored.Restore(continued);
    Check(continued.WorldId == worldId && continued.NextSequence == 4
        && continued.RuntimeFirstSequence == 3 && continued.LastTick == 1
        && continued.History.back().Sequence == 3 && continued.LastSource == api::FactId{41, 1}
        && runtimeRestored.Export().LastSource == continued.LastSource,
        "reused runtime identity changed the stable world ID, journal sequence or restore result");
}

void CreateGarden(GameSession &session) {
    Check(session.Control(NativeCaller::Driver, api::CreateForceGarden{}).Succeeded(),
        "force garden scene creation failed");
}

void PendingMeasurementIsNotLearned() {
    TemporarySaveRoot saveRoot;
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    GameSession session(true, generation);
    CreateGarden(session);
    const auto player = session.GetPlayer()->GetWorldPosition().Cell();
    const auto firstTarget = api::Cell{GardenCrystalX, player.Y};
    Check(session.Execute(NativeCaller::Input, api::ObserveForce{firstTarget}).Applied(),
        "initial force measurement failed");
    const auto scope = api::Rect{firstTarget, 1, 1};

    auto &world = testing::SessionAccess::World(session);
    WindStrategySettings wind;
    wind.Kind = WindStrategyKind::Procedural;
    world.ConfigureWindDomain(256, wind);
    constexpr int64_t motionY = 504;
    for (const auto x : {600, 602, 604, 606})
        world.SetCell(x, motionY, {CellTypeId::Dirt, {0, 1}});
    world.SetBaseForce(600, motionY, {0, 3600});
    world.SetBaseForce(602, motionY, {3600, 0});
    world.SetBaseForce(604, motionY, {-3600, 0});
    world.SetBaseForce(606, motionY, {0, -3600});
    testing::WorldAccess::Map(world).Wake(world.GetSimulationBounds());
    testing::SessionAccess::PublishFixtureChanges(session);
    world.WaitForStreaming();
    session.Tick(0, {});
    const auto *stream = world.GetStreamer();
    Check(stream != nullptr, "pending measurement fixture did not enable streaming");
    world.WaitForStreaming();
    const auto activeBounds = world.GetSimulationBounds();
    for (const auto coordinate : testing::WorldAccess::Map(world).ChunkCoordinates()) {
        const WorldRect chunk{coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge,
            storage::ChunkEdge, storage::ChunkEdge};
        if (!chunk.Intersects(activeBounds)) testing::WorldAccess::Map(world).Remove(coordinate);
    }
    world.SetPublicationBytes(stream->Settings().BudgetBytes - stream->RetainedBytes() - 128);
    world.WaitForStreaming();
    session.Tick(0, {});
    const auto oldSnapshot = session.Observe(NativeCaller::Driver, scope);
    const auto before = session.CaptureKnowledgeData();
    Check(oldSnapshot.Status == api::ObservationStatus::Available && oldSnapshot.Snapshot
        && oldSnapshot.Snapshot->Knowledge.HighWatermark == before.NextSequence - 1,
        "initial force measurement was missing from the captured knowledge snapshot");
    Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(),
        "pending measurement fixture step request failed");
    const auto stamp = session.CurrentCommit();
    session.Tick(1.f / 60.f, {});
    Check(world.GetEnvironment().IsWaitingForBudget() && session.CurrentCommit() == stamp,
        "pending measurement fixture did not enter a real budget-blocked step");

    const auto refused = session.Execute(NativeCaller::Input,
        api::ObserveForce{{GardenCrystalX + 1, player.Y}});
    const auto afterRefusal = session.CaptureKnowledgeData();
    const auto frozen = session.Observe(NativeCaller::Driver, scope);
    Check(!refused.Applied() && refused.Rejection == api::Reason::WaitingForBudget,
        "budget-pending force request was not refused with WaitingForBudget");
    Check(afterRefusal.NextSequence == before.NextSequence
        && afterRefusal.LastSource == before.LastSource
        && afterRefusal.History.size() == before.History.size()
        && afterRefusal.Discoveries.size() == before.Discoveries.size(),
        "budget-pending force request changed persistent knowledge");
    Check(frozen.Status == api::ObservationStatus::Available && frozen.Snapshot,
        "budget-pending retry did not return the cached observation");
    Check(SameObservationKnowledge(*oldSnapshot.Snapshot, *frozen.Snapshot),
        "budget-pending retry changed the cached knowledge snapshot");

    world.SetPublicationBytes(0);
    session.Tick(.1f, {});
    Check(!world.GetEnvironment().IsWaitingForBudget() && session.CurrentCommit().Revision == stamp.Revision + 1,
        "pending measurement step did not recover as one committed step");
    const auto resumed = session.Execute(NativeCaller::Input,
        api::ObserveForce{{GardenCrystalX + 1, player.Y}});
    const auto afterRecovery = session.CaptureKnowledgeData();
    Check(resumed.Applied() && std::holds_alternative<api::ForceObserved>(resumed.Result)
        && std::abs(std::get<api::ForceObserved>(resumed.Result).AccelerationCellsPerSecondSquared[0] - 3600.0) < 0.01
        && afterRecovery.History.size() == before.History.size() + 1
        && afterRecovery.Discoveries.size() == before.Discoveries.size() + 1
        && afterRecovery.Discoveries.back().Location == api::Cell{GardenCrystalX + 1, player.Y},
        "recovered force measurement did not create exactly one new discovery");
}

void GameSessionKnowledgeFlow() {
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    generation.Seed = 0x4d352e31;
    GameSession session(false, generation);
    CreateGarden(session);

    const auto player = session.GetPlayer()->GetWorldPosition().Cell();
    const auto beforeObservation = session.CaptureKnowledgeData();
    const auto view = session.Observe(NativeCaller::Input, {{player.X - 2, player.Y - 2}, 5, 5});
    Check(view.Status == api::ObservationStatus::Available
        && session.CaptureKnowledgeData().History.size() == beforeObservation.History.size(),
        "read-only observation unexpectedly taught knowledge");

    const auto target = api::Cell{GardenCrystalX, player.Y};
    const auto receipt = session.Execute(NativeCaller::Input, api::ObserveForce{target});
    Check(receipt.Applied() && std::holds_alternative<api::ForceObserved>(receipt.Result),
        "nearby player force measurement was rejected");
    const auto measured = std::get<api::ForceObserved>(receipt.Result);
    Check(measured.Location == target && std::abs(measured.AccelerationCellsPerSecondSquared[0] - 3600.0) < 0.01,
        "force garden did not provide the authored horizontal 3600 sample");
    const auto learned = session.CaptureKnowledgeData();
    Check(learned.History.size() == 1 && learned.Discoveries.size() == 1
        && learned.Discoveries[0].Observed && !learned.Discoveries[0].Analyzed
        && !learned.Discoveries[0].CanCopy && !learned.Discoveries[0].CanEdit,
        "player measurement did not create observation-only knowledge");
    const auto firstFact = learned.History.back().Source;
    const auto captured = session.CaptureObservation({target, 1, 1});
    Check(captured.Status == api::ObservationStatus::Available && captured.Snapshot
        && receipt.ResolvedAt == firstFact.CommittedAt
        && captured.Snapshot->Stamp == receipt.ResolvedAt
        && captured.Snapshot->Knowledge.Stamp == captured.Snapshot->Stamp,
        "measurement fact, receipt and captured knowledge did not share one commit stamp");

    const api::ActionRequest duplicate{receipt.Key.WorldEpoch, receipt.Key.Sequence, api::ObserveForce{target}};
    const auto repeated = session.Submit(receipt.Key.Caller, duplicate);
    Check(repeated.Applied() && repeated.Facts.FirstSequence == receipt.Facts.FirstSequence
        && session.CaptureKnowledgeData().History.size() == learned.History.size(),
        "duplicate measurement request was committed or learned twice");
    const auto secondMeasurement = session.Execute(NativeCaller::Input, api::ObserveForce{target});
    const auto afterRepeatedLocation = session.CaptureKnowledgeData();
    Check(secondMeasurement.Applied() && afterRepeatedLocation.History.size() == learned.History.size()
        && afterRepeatedLocation.Discoveries.size() == 1
        && afterRepeatedLocation.Discoveries.front().EvidenceSequence == learned.Discoveries.front().EvidenceSequence,
        "a new measurement request for a discovered location duplicated history or replaced its first evidence");

    const auto far = session.Execute(NativeCaller::Input,
        api::ObserveForce{{player.X + 33, player.Y}});
    Check(!far.Applied() && far.Rejection == api::Reason::PermissionDenied,
        "measurement outside the 32-cell range was accepted");
    const auto missing = session.Execute(NativeCaller::Input,
        api::ObserveForce{{player.X + 1024, player.Y}});
    Check(!missing.Applied() && missing.Rejection == api::Reason::NotReady,
        "measurement of a nonresident cell was accepted");

    const auto surface = player.Y + int64_t(Body::Height);
    for (int64_t x = 516; x <= 521; ++x) {
        const auto material = x < 520 ? CellTypeId::Stone : CellTypeId::CopperOre;
        const auto mined = session.Execute(NativeCaller::Input,
            api::MineRegion{{x, surface + 2}, uint32_t(material)});
        Check(mined.Applied(), "force garden construction deposit could not be mined");
    }
    const auto placed = session.Execute(NativeCaller::Driver, api::PlaceDevice{{GardenDeviceX, surface - 2}});
    Check(placed.Applied(), "force emitter could not be placed after mining the garden deposit");
    const auto history = session.CaptureKnowledgeData().History;
    Check(std::any_of(history.begin(), history.end(), [](const auto &entry) {
        return std::holds_alternative<api::MaterialRemoved>(entry.Source.Payload);
    }) && std::any_of(history.begin(), history.end(), [](const auto &entry) {
        const auto *device = std::get_if<api::DeviceChanged>(&entry.Source.Payload);
        return device && device->Event == api::DeviceEvent::Placed;
    }), "real mining and device actions were absent from knowledge history");

    Check(session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded(), "world reset failed");
    const auto reset = session.CaptureKnowledgeData();
    Check(reset.History.empty() && reset.Discoveries.empty() && reset.NextSequence == 1
        && reset.WorldId.empty(), "world reset retained previous knowledge");
}

} // namespace

int main() {
    try {
        JournalSelectionAndOrdering();
        BoundedHistoryAndDiscoveries();
        CheckpointContract();
        GameSessionKnowledgeFlow();
        PendingMeasurementIsNotLearned();
        std::cout << "Pixel knowledge journal and session behavior passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
