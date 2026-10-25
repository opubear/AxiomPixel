#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/Actions/ActionValues.h"
#include "Runtime/Gameplay/GameAPI/Observations.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace pixel;
namespace {
formula::process::ProgramArtifact Program(std::string_view source) {
    return std::get<formula::process::ProgramArtifact>(formula::process::Compile("test.process", source));
}

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

const api::ObservationSnapshot &Available(const api::ObservationResult &result) {
    Require(result.Status == api::ObservationStatus::Available && result.Snapshot.has_value(),
            "observation was not available");
    return *result.Snapshot;
}

const api::DomainFact &FactOf(const api::FactReadResult &result, size_t index) {
    Require(index < result.Facts.size(), "fact index missing");
    return result.Facts[index];
}

void SnapshotValuesAndIncrementalSampling() {
    GameSession session;
    WindStrategySettings emptyWind;
    emptyWind.Kind = WindStrategyKind::Empty;
    testing::SessionAccess::World(session).ConfigureWindDomain(600, emptyWind);
    testing::SessionAccess::PublishFixtureChanges(session);
    const api::Rect scope{{504, 504}, 32, 16};
    const auto firstResult = session.Observe(NativeCaller::Driver, scope);
    const auto &first = Available(firstResult);
    Require(first.Cells.size() == size_t(scope.Width) * scope.Height
                && first.ResidentCellCount == scope.Width * scope.Height
                && first.MaterialAvailability() == api::Availability::Available,
            "resident material coverage was not available over the requested scope");
    const auto dirtIndex = size_t(512 - scope.Origin.Y) * scope.Width + size_t(520 - scope.Origin.X);
    const auto airIndex = size_t(510 - scope.Origin.Y) * scope.Width + size_t(520 - scope.Origin.X);
    Require(first.Cells[dirtIndex].MaterialId == uint32_t(CellTypeId::Dirt), "material sample lost Dirt");
    Require(first.Cells[airIndex].MaterialId == uint32_t(CellTypeId::Air)
                && first.Cells[airIndex].AirKind == api::AirSampleKind::EmptyBackend
                && first.Cells[airIndex].AirVelocity == std::array<float, 2>{0, 0},
            "raw air sample did not preserve the Empty backend's known zero wind");
    Require(first.Player.has_value() && first.Programs.Applied == session.AppliedProgram(),
            "player or applied program was omitted from the observation");
    Require(first.Programs.RunningRoots == 0 && first.Programs.Projectiles == 0 && first.Programs.Pending == 0,
            "runtime summary did not describe the empty runtime");

    const auto initialMetrics = session.ObservationStats();
    const auto sameResult = session.Observe(NativeCaller::Driver, scope);
    const auto sameMetrics = session.ObservationStats();
    Require(Available(sameResult).Stamp == first.Stamp
                && sameMetrics.MaterialCellsSampled == initialMetrics.MaterialCellsSampled
                && sameMetrics.WindCellsSampled == initialMetrics.WindCellsSampled,
            "same-stamp capture resampled cells");

    const auto mined = session.Execute(NativeCaller::Driver,
        api::MineRegion{{520, 512}, uint32_t(CellTypeId::Dirt)});
    Require(mined.Applied() && mined.Facts.Count == 1, "material action did not publish one fact");
    const auto newer = session.Observe(NativeCaller::Driver, scope);
    const auto &updated = Available(newer);
    const auto updatedMetrics = session.ObservationStats();
    Require(updated.Stamp.Revision == first.Stamp.Revision + 1
                && first.Cells[dirtIndex].MaterialId == uint32_t(CellTypeId::Dirt)
                && updated.Cells[dirtIndex].MaterialId == uint32_t(CellTypeId::Air)
                && updated.Cells[dirtIndex].MaterialSampledAtRevision == updated.Stamp.Revision,
            "returned snapshot was not frozen or new material was not stamped");
    Require(updatedMetrics.MaterialCellsSampled > sameMetrics.MaterialCellsSampled
                && updatedMetrics.MaterialCellsSampled - sameMetrics.MaterialCellsSampled < scope.Width * scope.Height
                && updatedMetrics.WindCellsSampled - sameMetrics.WindCellsSampled < scope.Width * scope.Height,
            "single region change did not produce an incremental sample count");

    const auto chunkCount = session.GetWorld().GetMap().ChunkCount();
    const auto missing = session.Observe(NativeCaller::Input, {{-16, -16}, 8, 8});
    Require(Available(missing).Cells.front().MaterialId == std::nullopt
                && missing.Snapshot->ResidentCellCount == 0
                && missing.Snapshot->MaterialAvailability() == api::Availability::Partial,
            "negative nonresident coordinate was not represented as unknown");
    // Keep room below the 49-bit chunk-coordinate limit when ChunkEdge is 64.
    const int64_t far = (int64_t{1} << 55) - 8192;
    const auto farPositive = session.Observe(NativeCaller::Debug, {{far, far}, 8, 8});
    const auto farNegative = session.Observe(NativeCaller::Debug, {{-far, -far}, 8, 8});
    Require(farPositive.Status == api::ObservationStatus::Available && farPositive.Snapshot
                && farNegative.Status == api::ObservationStatus::Available && farNegative.Snapshot
                && std::all_of(farPositive.Snapshot->Cells.begin(), farPositive.Snapshot->Cells.end(),
                               [](const auto &cell) { return !cell.MaterialId; })
                && std::all_of(farNegative.Snapshot->Cells.begin(), farNegative.Snapshot->Cells.end(),
                               [](const auto &cell) { return !cell.MaterialId; })
                && session.GetWorld().GetMap().ChunkCount() == chunkCount,
            "remote observation loaded a chunk or failed to report missing cells");
    const auto tooLarge = session.Observe(NativeCaller::Debug, {{0, 0}, 65, 1});
    Require(tooLarge.Status == api::ObservationStatus::LimitExceeded,
            "observation exceeded the public 64-cell edge bound");
}

void FarResidentProjectionAndChunkLifecycle() {
    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    world.DiscardStreaming();
    const int64_t baseX = (int64_t{1} << 55) - 8192 + 17;
    const int64_t baseY = -((int64_t{1} << 55) - 8192) - 17;
    const CellCoord base{baseX, baseY};
    const auto chunk = ChunkOf(base);
    testing::WorldAccess::Map(world).Insert(std::make_unique<WorldChunk>(chunk));
    world.SetCell(baseX + 10, baseY + 10, {CellTypeId::Dirt, {0, 1}});
    testing::SessionAccess::SetPlayerPosition(session, {chunk, {double(baseX - chunk.X * storage::ChunkEdge) + .125,
                                 double(baseY - chunk.Y * storage::ChunkEdge) + .125}});
    testing::SessionAccess::PublishFixtureChanges(session);
    const api::Rect scope{{baseX, baseY}, 16, 16};
    auto result = session.Observe(NativeCaller::Driver, scope);
    const auto &snapshot = Available(result);
    const auto dirt = snapshot.Cells[size_t(10) * scope.Width + 10];
    Require(dirt.MaterialId == uint32_t(CellTypeId::Dirt) && snapshot.Player
                && snapshot.Player->Location.AnchorCell == api::Cell{base.X, base.Y}
                && snapshot.Player->Location.Offset == std::array<double, 2>{.125, .125}
                && snapshot.ResidentCellCount == scope.Width * scope.Height
                && snapshot.MaterialAvailability() == api::Availability::Available,
            "resident far coordinate or fractional player position lost precision");

    testing::WorldAccess::Map(world).Remove(chunk);
    testing::SessionAccess::PublishFixtureChanges(session);
    const auto removed = Available(session.Observe(NativeCaller::Driver, scope));
    Require(!removed.Cells[size_t(10) * scope.Width + 10].MaterialId
                && removed.ResidentCellCount == 0
                && removed.MaterialAvailability() == api::Availability::Partial,
            "removed chunk retained a stale material sample");

    testing::WorldAccess::Map(world).Insert(std::make_unique<WorldChunk>(chunk));
    world.SetCell(baseX + 10, baseY + 10, {CellTypeId::Dirt, {0, 1}});
    testing::SessionAccess::PublishFixtureChanges(session);
    const auto restored = Available(session.Observe(NativeCaller::Driver, scope));
    Require(restored.Cells[size_t(10) * scope.Width + 10].MaterialId == uint32_t(CellTypeId::Dirt)
                && restored.ResidentCellCount == scope.Width * scope.Height
                && restored.MaterialAvailability() == api::Availability::Available,
            "reinserted chunk did not restore resident observation data");
}

struct TemporaryWorld {
    std::filesystem::path Directory = std::filesystem::temp_directory_path()
        / ("pixel-observation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryWorld() { std::filesystem::remove_all(Directory); }
};

void PendingStepUsesOnlyCompleteCopies() {
    TemporaryWorld files;
    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    WorldStreamSettings settings;
    settings.Directory = files.Directory;
    settings.WindowEdge = 256;
    settings.SurfaceY = 10000;
    settings.BudgetBytes = 50ull * 1024 * 1024;
    world.EnableStreaming(settings);
    testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(191.9, 128));
    WindStrategySettings wind;
    wind.Kind = WindStrategyKind::Procedural;
    world.ConfigureWindDomain(256, wind);
    world.SetCell(32, 10, {CellTypeId::Dirt, {0, 1}});
    testing::SessionAccess::PublishFixtureChanges(session);
    session.Control(NativeCaller::Driver, api::SetPaused{false});

    const auto source = world.InstallFieldSource(999, {WorldPosition::FromAbsolute(100, 100), 2, {100, 0}, 20});
    Require(source.has_value(), "pending source setup failed");
    testing::SessionAccess::PublishFixtureChanges(session);

    const api::Rect scope{{64, 64}, 64, 64};
    world.WaitForStreaming();
    world.WaitForStreaming();
    for (const auto coordinate : world.GetMap().ChunkCoordinates())
        if (!world.GetSimulationBounds().Contains({coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge}))
            testing::WorldAccess::Map(world).Remove(coordinate);
    testing::SessionAccess::PublishFixtureChanges(session);
    const auto stamp = session.CurrentCommit();
    const auto initial = session.Observe(NativeCaller::Driver, scope);
    const auto frozenStamp = Available(initial).Stamp;
    const auto sampleIndex = size_t(100 - scope.Origin.Y) * scope.Width + size_t(100 - scope.Origin.X);
    const auto frozenWind = Available(initial).Cells[sampleIndex].AirVelocity;
    const auto frozenSource = Available(initial).Gameplay.FieldSources.at(0);
    Require(frozenWind.has_value(), "pending fixture air cell was not sampled");
    world.SetPublicationBytes(settings.BudgetBytes - world.GetStreamer()->RetainedBytes() - 128);
    session.Tick(.1f, {1, false});
    Require(world.GetEnvironment().IsWaitingForBudget(), "fixture did not enter pending-step state");
    const auto preparedWind = world.SampleWind(CellCoord{100, 100});
    Require(frozenWind != std::array<float, 2>{preparedWind.X, preparedWind.Y},
            "pending fixture did not alter live wind behind the committed snapshot");

    const auto same = session.Observe(NativeCaller::Driver, scope);
    const auto other = session.Observe(NativeCaller::Input, {{0, 0}, 16, 16});
    const auto service = session.ObservationService();
    Require(Available(same).Stamp == stamp && Available(same).Stamp == frozenStamp
                && other.Status == api::ObservationStatus::NotSampled && !other.Snapshot
                && service.LastCommitted == stamp && service.SessionReady && service.WaitingForBudget,
            "pending step exposed prepared state or sampled an uncached scope, or service status was stale");
    const auto unchangedMetrics = session.ObservationStats();
    session.Tick(.2f, {-1, true});
    const auto retry = session.Observe(NativeCaller::Driver, scope);
    Require(Available(retry).Stamp == stamp && session.CurrentCommit() == stamp
                && session.ObservationStats().MaterialCellsSampled == unchangedMetrics.MaterialCellsSampled
                && session.ObservationStats().WindCellsSampled == unchangedMetrics.WindCellsSampled
                && Available(retry).Cells[sampleIndex].AirVelocity == frozenWind,
            "pending retry changed the complete snapshot or sampled its contents");
    Require(Available(retry).Gameplay.Stamp == stamp
        && Available(retry).Gameplay.FieldSources.at(0).RemainingSteps == frozenSource.RemainingSteps,
        "pending step leaked uncommitted source lifetime");
    world.SetPublicationBytes(0);
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(.2f, {-1, true});
    Require(!world.GetEnvironment().IsWaitingForBudget() && session.CurrentCommit().Revision == stamp.Revision + 1,
            "budget recovery did not publish one complete environment step");
    const auto recovered = Available(session.Observe(NativeCaller::Driver, scope));
    const auto committedWind = world.SampleWind(CellCoord{100, 100});
    Require(recovered.Stamp == session.CurrentCommit()
                && recovered.Cells[sampleIndex].AirVelocity == std::array<float, 2>{committedWind.X, committedWind.Y}
                && recovered.Cells[sampleIndex].AirVelocity != frozenWind,
            "recovered observation did not refresh to the complete committed wind state");
}

void ProgramIdentityAndEpochReset() {
    GameSession session;
    const api::Rect scope{{500, 500}, 64, 64};
    auto firstCast = session.Execute(NativeCaller::Driver,
        api::ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}});
    Require(firstCast.Applied(), "first program cast failed");
    auto first = Available(session.Observe(NativeCaller::Driver, scope));
    Require(first.Projectiles.size() == 1, "first cast produced no projected projectile");
    const auto firstId = first.Projectiles.front().InstanceId;
    Require(session.Control(NativeCaller::Driver, api::ClearRunning{}).Succeeded(), "clear running failed");
    auto cleared = Available(session.Observe(NativeCaller::Driver, scope));
    Require(cleared.Projectiles.empty(), "cleared program remained in the observation");
    auto secondCast = session.Execute(NativeCaller::Driver,
        api::ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}});
    Require(secondCast.Applied(), "second program cast failed");
    auto second = Available(session.Observe(NativeCaller::Driver, scope));
    Require(second.Projectiles.size() == 1 && second.Projectiles.front().InstanceId != firstId,
            "runtime reused a deleted cast identity");

    const auto oldEpoch = session.WorldEpoch();
    session.Control(NativeCaller::Driver, api::ResetWorld{});
    Require(session.WorldEpoch() != oldEpoch && session.FactHighWatermark() == 0,
            "epoch reset retained identity or fact watermark");
    const auto reset = Available(session.Observe(NativeCaller::Driver, scope));
    Require(reset.Stamp.WorldEpoch == session.WorldEpoch() && reset.Stamp.Revision == 1
                && reset.Projectiles.empty() && reset.FactHighWatermark == 0,
            "reset reused an observation cache or fact sequence");
    Require(session.ReadFacts(oldEpoch, 0, 0).Status == api::FactReadStatus::StaleEpoch,
            "old epoch fact cursor was accepted after reset");
}

void FactRangesCursorsAndWindSemantics() {
    GameSession session;
    const auto caller = session.CallerSession(NativeCaller::Driver);
    const auto first = session.Submit(caller, {session.WorldEpoch(), 1,
        api::MineRegion{{520, 512}, uint32_t(CellTypeId::Dirt)}});
    Require(first.Applied() && first.Facts.Count == 1, "successful mine lacks a one-fact range");
    const auto duplicate = session.Submit(caller, {session.WorldEpoch(), 1,
        api::MineRegion{{520, 512}, uint32_t(CellTypeId::Dirt)}});
    Require(duplicate.Facts.WorldEpoch == first.Facts.WorldEpoch
                && duplicate.Facts.FirstSequence == first.Facts.FirstSequence
                && duplicate.Facts.Count == first.Facts.Count && session.FactHighWatermark() == 1,
            "duplicate request duplicated its committed fact");
    const auto rejected = session.Submit(caller, {session.WorldEpoch(), 2,
        api::MineRegion{{520, 512}, uint32_t(CellTypeId::Dirt)}});
    Require(!rejected.Applied() && rejected.Facts.Count == 0 && session.FactHighWatermark() == 1,
            "rejected action emitted a fact");
    const auto result = session.ReadFacts(session.WorldEpoch(), 0, session.FactHighWatermark(), 256);
    Require(result.Status == api::FactReadStatus::Available && result.Facts.size() == 1
                && FactOf(result, 0).Id.Sequence == first.Facts.FirstSequence
                && std::holds_alternative<api::RequestKey>(FactOf(result, 0).Cause)
                && std::get<api::RequestKey>(FactOf(result, 0).Cause) == first.Key
                && std::holds_alternative<api::MaterialRemoved>(FactOf(result, 0).Payload),
            "fact read lost its stable sequence, request cause, or material payload");
    Require(session.ReadFacts(session.WorldEpoch(), 0, 0).Facts.empty(),
            "fixed high watermark admitted a later fact");

    const auto beforeWindConfiguration = session.FactHighWatermark();
    Require(session.Execute(NativeCaller::Driver, api::SelectWindStrategy{api::WindKind::Procedural}).Applied(),
            "wind strategy selection failed");
    Require(session.Execute(NativeCaller::Driver, api::ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}}).Applied(),
            "wind fixture cast failed");
    const auto afterCast = session.FactHighWatermark();
    const auto configuredFacts = session.ReadFacts(session.WorldEpoch(), beforeWindConfiguration, afterCast);
    Require(configuredFacts.Facts.size() == 2
                && std::holds_alternative<api::WindConfigured>(configuredFacts.Facts[0].Payload)
                && std::holds_alternative<api::ProgramActivated>(configuredFacts.Facts[1].Payload),
            "wind configuration and activation facts were missing or out of order");

    // Procedural is unsupported by the wind-pulse adapter. The cast may still
    // apply, but its skipped wind effect must not be described as submitted.
    const auto applied = session.ApplyProcess(NativeCaller::Driver, Program("process 1; emit wind(cells(1), speed(120), speed(0));"));
    Require(bool(applied), "wind formula could not be applied");
    const auto beforeUnsupported = session.FactHighWatermark();
    const auto unsupportedCast = session.Execute(NativeCaller::Driver,
        api::ActivateProgram{*applied.Program, {{600, 510}, {0, 0}}});
    Require(unsupportedCast.Applied() && session.FactHighWatermark() == beforeUnsupported + 1,
            "unsupported wind should produce activation only, not a wind submission fact");
    const auto unsupportedFacts = session.ReadFacts(session.WorldEpoch(), beforeUnsupported,
        session.FactHighWatermark()).Facts;
    Require(unsupportedFacts.size() == 1 && std::holds_alternative<api::ProgramActivated>(unsupportedFacts[0].Payload),
            "unsupported wind emitted a false WindPulseSubmitted fact");
    const auto stableHighWatermark = session.FactHighWatermark();
    const auto noOpFormula = session.ApplyProcess(NativeCaller::Driver, Program("process 1; emit dig(cells(2));"));
    Require(bool(noOpFormula), "zero-change dig formula could not be applied");
    const auto noOpCast = session.Execute(NativeCaller::Driver,
        api::ActivateProgram{*noOpFormula.Program, {{600, 100}, {0, 0}}});
    Require(noOpCast.Applied() && noOpCast.Facts.Count == 1
                && session.FactHighWatermark() == stableHighWatermark + 1,
            "zero-change dig emitted a material fact");

    // The activation fact is in the captured fixed high-watermark window, and
    // later facts must not leak into a reader that still uses that old bound.
    const auto fixedWindow = session.ReadFacts(session.WorldEpoch(), beforeUnsupported,
        beforeUnsupported + 1);
    Require(fixedWindow.Facts.size() == 1 && fixedWindow.HighWatermark == beforeUnsupported + 1
                && std::holds_alternative<api::ProgramActivated>(fixedWindow.Facts.front().Payload),
            "fact reader crossed its supplied high watermark");

    api::ObservationSnapshot fake;
    fake.Scope = {{0, 0}, 4, 1};
    fake.Cells.resize(4);
    fake.Cells[0] = {uint32_t(CellTypeId::Air), api::AirSampleKind::Sampled, std::array<float, 2>{2, 0}, 0, 4};
    fake.Cells[1] = {uint32_t(CellTypeId::Air), api::AirSampleKind::Sampled, std::array<float, 2>{0, 4}, 0, 5};
    fake.Cells[2] = {uint32_t(CellTypeId::Dirt), api::AirSampleKind::NotApplicable, std::nullopt, 0, 0};
    fake.Cells[3] = {std::nullopt, api::AirSampleKind::NotResident, std::nullopt, 0, 0};
    const auto summaries = api::SummarizeWind(fake);
    Require(summaries.size() == 1 && summaries[0].MeanVelocityCellsPerSecond == std::array<double, 2>{1, 2}
                && summaries[0].AirSamples == 2 && summaries[0].ResidentCells == 3
                && summaries[0].CoverageFraction() == .75 && summaries[0].UnknownCells == 1,
            "wind summary included non-air/missing values or used the wrong coverage denominator");
    fake.Cells[0].MaterialId = uint32_t(CellTypeId::Dirt);
    fake.Cells[0].AirVelocity.reset();
    fake.Cells[0].AirKind = api::AirSampleKind::NotApplicable;
    fake.Cells[1] = fake.Cells[0];
    const auto noAir = api::SummarizeWind(fake);
    Require(noAir[0].Status == api::Availability::Partial && !noAir[0].MeanVelocityCellsPerSecond,
            "no-air summary manufactured a zero mean");

    GameSession emissionSession;
    WindStrategySettings coarse;
    coarse.Kind = WindStrategyKind::Coarse;
    coarse.Coarse.Columns = coarse.Coarse.Rows = 16;
    testing::SessionAccess::World(emissionSession).ConfigureWindDomain(600, coarse);
    testing::SessionAccess::PublishFixtureChanges(emissionSession);
    const auto delayed = emissionSession.ApplyProcess(NativeCaller::Driver, Program("process 1; emit wind(cells(1), speed(60), speed(0)); wait ticks(2); emit wind(cells(1), speed(60), speed(0));"));
    Require(bool(delayed), "cross-tick wind formula could not be applied");
    const auto activation = emissionSession.Execute(NativeCaller::Driver,
        api::ActivateProgram{*delayed.Program, {{600, 100}, {0, 0}}});
    Require(activation.Applied() && activation.Facts.Count == 2,
            "immediate committed wind effect was not attached to its activation receipt");
    for (int step = 0; step < 2; ++step) {
        emissionSession.Control(NativeCaller::Driver, api::StepSimulation{});
        emissionSession.Tick(1.0f / 60.0f, {});
    }
    const auto emissions = emissionSession.ReadFacts(emissionSession.WorldEpoch(), activation.Facts.FirstSequence - 1,
        emissionSession.FactHighWatermark());
    Require(emissions.Facts.size() == 3 && std::holds_alternative<api::ProgramActivated>(emissions.Facts[0].Payload)
                && std::holds_alternative<api::WindPulseSubmitted>(emissions.Facts[1].Payload)
                && std::holds_alternative<api::WindPulseSubmitted>(emissions.Facts[2].Payload),
            "successful wind emissions were not recorded across activation and simulation commits");
    const auto &firstCause = std::get<api::ProgramEffectCause>(emissions.Facts[1].Cause);
    const auto &laterCause = std::get<api::ProgramEffectCause>(emissions.Facts[2].Cause);
    Require(firstCause.ActivationRequest == activation.Key && laterCause.ActivationRequest == activation.Key
                && firstCause.EmissionSequence < laterCause.EmissionSequence
                && firstCause.Instance == laterCause.Instance,
            "later effect lost its activation request or stable per-root emission identity");

    GameSession overflowSession;
    for (int index = 0; index < 257; ++index)
        overflowSession.Execute(NativeCaller::Driver,
            api::SetWindBoundary{GameSession::DefaultWindStartHeight + index + 1});
    const auto overflow = overflowSession.ReadFacts(overflowSession.WorldEpoch(), 0,
        overflowSession.FactHighWatermark());
    Require(overflow.Status == api::FactReadStatus::Gap && overflow.FirstAvailableSequence > 1,
            "bounded fact ring did not report an overflow gap");
}

void BoundedCapacityAndCallerOwnership() {
    GameSession session;
    const api::Rect full{{0, 0}, 64, 64};
    auto first = Available(session.Observe(NativeCaller::Driver, full));
    const auto firstBytes = first.RetainedBytes();
    const auto retained = session.ObservationStats();
    Require(retained.ActiveScopes == 1 && retained.RetainedBytes >= firstBytes
                && retained.RetainedBytes <= retained.MaximumRetainedBytes,
            "observation retained memory escaped its advertised budget");
    auto second = Available(session.Observe(NativeCaller::Input, {{64, 0}, 64, 64}));
    auto third = Available(session.Observe(NativeCaller::Debug, {{0, 64}, 64, 64}));
    Require(second.Cells.size() == 4096 && third.Cells.size() == 4096
                && session.ObservationStats().ActiveScopes == 3,
            "per-caller scope limit or 64x64 payload bound failed");
    session.CloseObservation(NativeCaller::Input);
    Require(session.ObservationStats().ActiveScopes == 2,
            "closing one caller did not release its observation scope");
    const auto oversized = session.Observe(NativeCaller::Input, {{0, 0}, 64, 64});
    Require(Available(oversized).Cells.capacity() <= 4096
                && Available(oversized).RetainedBytes() <= firstBytes + sizeof(api::ObservationSnapshot) + 4096 * sizeof(api::ObservedCell),
            "scope replacement exceeded a single bounded snapshot allocation");
}
} // namespace

int main() {
    try {
        SnapshotValuesAndIncrementalSampling();
        FarResidentProjectionAndChunkLifecycle();
        PendingStepUsesOnlyCompleteCopies();
        ProgramIdentityAndEpochReset();
        FactRangesCursorsAndWindSemantics();
        BoundedCapacityAndCallerOwnership();
        std::cout << "Observation and fact contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
