#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/LLMView/QueryProjection.h"
#include "Runtime/Gameplay/LLMView/ViewService.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace pixel;
namespace {
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

class FakeSource final : public api::ObservationSource {
  public:
    api::ObservationSnapshot Snapshot;
    bool Ready = true, Waiting = false;
    uint32_t CaptureCount = 0, ReadCount = 0;
    uint64_t LastReadHigh = 0;
    uint64_t FirstFact = 1;

    FakeSource(api::Rect scope = {{0, 0}, 64, 64}) { SetSnapshot(scope); }

    void SetSnapshot(api::Rect scope) {
        Snapshot = {};
        Snapshot.Stamp = {"fake-world", 7, 0, Snapshot.Stamp.Revision + 1};
        Snapshot.Scope = scope;
        Snapshot.FactHighWatermark = 3;
        Snapshot.Paused = true;
        Snapshot.Wind.Backend = api::WindKind::Coarse;
        Snapshot.Wind.Enabled = true;
        Snapshot.Wind.Domain = {{0, 0}, 64, 64};
        Snapshot.Cells.resize(size_t(scope.Width) * scope.Height);
        for (uint32_t y = 0; y < scope.Height; ++y) {
            for (uint32_t x = 0; x < scope.Width; ++x) {
                auto &cell = Snapshot.Cells[size_t(y) * scope.Width + x];
                cell.MaterialId = 1;
                cell.AirKind = api::AirSampleKind::Sampled;
                cell.AirVelocity = std::array<float, 2>{float(x), float(y)};
                cell.MaterialSampledAtRevision = Snapshot.Stamp.Revision;
                cell.WindSampledAtRevision = Snapshot.Stamp.Revision;
            }
        }
        Snapshot.ResidentCellCount = uint32_t(Snapshot.Cells.size());
        Snapshot.ObjectSampledAtRevision = Snapshot.Stamp.Revision;
        Snapshot.Programs.Applied = {42, 5};
        Snapshot.Gameplay.Stamp = Snapshot.Stamp;
    }

    void Commit(uint64_t revision, uint64_t tick = 0) {
        Snapshot.Stamp.Revision = revision;
        Snapshot.Stamp.Tick = tick;
        Snapshot.Gameplay.Stamp = Snapshot.Stamp;
        for (auto &cell : Snapshot.Cells) {
            cell.MaterialSampledAtRevision = revision;
            cell.WindSampledAtRevision = revision;
        }
        Snapshot.ObjectSampledAtRevision = revision;
        Snapshot.FactHighWatermark += 1;
    }

    api::ObservationResult CaptureObservation(api::Rect scope) override {
        ++CaptureCount;
        if (!llm::Contains(Snapshot.Scope, scope)) return {api::ObservationStatus::NotSampled};
        auto captured = Snapshot;
        captured.Scope = scope;
        captured.Cells.clear();
        captured.Cells.reserve(size_t(scope.Width) * scope.Height);
        captured.ResidentCellCount = 0;
        for (uint32_t y = 0; y < scope.Height; ++y) {
            for (uint32_t x = 0; x < scope.Width; ++x) {
                const auto sourceX = size_t(scope.Origin.X - Snapshot.Scope.Origin.X + x);
                const auto sourceY = size_t(scope.Origin.Y - Snapshot.Scope.Origin.Y + y);
                const auto &cell = Snapshot.Cells[sourceY * Snapshot.Scope.Width + sourceX];
                captured.Cells.push_back(cell);
                captured.ResidentCellCount += cell.MaterialId.has_value();
            }
        }
        return {api::ObservationStatus::Available, std::move(captured)};
    }

    api::ObservationServiceStatus ObservationService() const override {
        return {Snapshot.Stamp, Ready, Waiting};
    }

    api::FactReadResult ReadFacts(uint64_t epoch, uint64_t after, uint64_t high,
                                  uint32_t limit = 256) const override {
        auto *self = const_cast<FakeSource *>(this);
        ++self->ReadCount;
        self->LastReadHigh = high;
        api::FactReadResult result;
        result.WorldEpoch = epoch;
        result.HighWatermark = high;
        result.FirstAvailableSequence = FirstFact;
        result.NextSequence = after;
        if (epoch != Snapshot.Stamp.WorldEpoch) {
            result.Status = api::FactReadStatus::StaleEpoch;
            return result;
        }
        if (after < FirstFact - 1) {
            result.Status = api::FactReadStatus::Gap;
            return result;
        }
        const auto end = std::min(high, Snapshot.FactHighWatermark);
        for (uint64_t sequence = after + 1; sequence <= end && result.Facts.size() < limit; ++sequence) {
            api::DomainFact fact;
            fact.Id = {epoch, sequence};
            fact.CommittedAt = Snapshot.Stamp;
            fact.Payload = api::MaterialRemoved{{int64_t(sequence), 0}, 1, 0};
            result.Facts.push_back(std::move(fact));
            result.NextSequence = sequence;
        }
        result.HasMore = result.NextSequence < end;
        return result;
    }
};

api::Rect FullScope() { return {{0, 0}, 64, 64}; }

llm::Response Deliver(llm::ViewService &service, llm::ObserverId observer,
                      llm::PreparedResponse prepared) {
    if (prepared.Result != llm::Status::Ok || !prepared.Delivery)
        throw std::runtime_error(std::string("response was not prepared: ") + llm::StatusName(prepared.Result));
    return service.Deliver(observer, prepared.Delivery);
}

llm::QuerySpec Materials(api::Rect scope = {{0, 0}, 1, 1}, uint32_t pageSize = 1) {
    llm::QuerySpec query;
    query.Topic = api::Topic::Materials;
    query.Scope = scope;
    query.PageSize = pageSize;
    return query;
}

void FrozenViewsAndCursorBinding() {
    FakeSource source;
    llm::ViewService service(source);
    const auto observer = service.RegisterObserver(llm::Policy::FullAccess());
    const auto opened = service.OpenView(observer, FullScope());
    Require(opened.Result == llm::Status::Ok && opened.View, "initial view did not open");
    const auto firstView = *opened.View;

    auto query = Materials({{0, 0}, 16, 1}, 1);
    auto first = Deliver(service, observer, service.Query(observer, firstView.Id, query));
    Require(first.Result == llm::Status::Ok && first.Page && first.Page->View.Stamp.Revision == 1
                && first.Page->Cells.front().MaterialId == 1 && first.Page->NextCursor,
            "first page did not come from the frozen initial commit");
    const auto cursor = first.Page->NextCursor;
    source.Commit(2);
    auto second = Deliver(service, observer, service.Query(observer, firstView.Id, query, cursor));
    Require(second.Result == llm::Status::Ok && second.Page && second.Page->View.Stamp.Revision == 1,
            "same view mixed in a later commit");

    auto changed = query;
    changed.Topic = api::Topic::AirVelocity;
    Require(service.Query(observer, firstView.Id, changed, cursor).Result == llm::Status::InvalidCursor,
            "cursor was reusable for a different topic");
    changed = query;
    changed.Scope.Width -= 1;
    Require(service.Query(observer, firstView.Id, changed, cursor).Result == llm::Status::InvalidCursor,
            "cursor was reusable for a different scope");
    changed = query;
    changed.ResolutionCells = 2;
    Require(service.Query(observer, firstView.Id, changed, cursor).Result == llm::Status::Unsupported,
            "unsupported resolution did not fail before cursor use");
    changed = query;
    changed.PageSize = 2;
    Require(service.Query(observer, firstView.Id, changed, cursor).Result == llm::Status::InvalidCursor,
            "cursor was reusable with a different page size");
    llm::QuerySpec windPage;
    windPage.Topic = api::Topic::AirVelocity;
    windPage.Scope = {{0, 0}, 8, 1};
    windPage.PageSize = 1;
    auto windFirst = Deliver(service, observer, service.Query(observer, firstView.Id, windPage));
    Require(windFirst.Page && windFirst.Page->NextCursor,
            "fine wind query did not produce its next page cursor");
    auto changedResolution = windPage;
    changedResolution.ResolutionCells = 16;
    Require(service.Query(observer, firstView.Id, changedResolution, windFirst.Page->NextCursor).Result
                == llm::Status::InvalidCursor,
            "cursor was reusable with a different resolution");
    changed = query;
    changed.SchemaVersion = 2;
    Require(service.Query(observer, firstView.Id, changed, cursor).Result == llm::Status::InvalidArgument,
            "unsupported schema version was accepted");

    const auto replacement = service.OpenView(observer, FullScope());
    Require(replacement.Result == llm::Status::Ok && replacement.View
                && replacement.View->Id != firstView.Id && replacement.View->Stamp.Revision == 2
                && service.Query(observer, firstView.Id, query).Result == llm::Status::ViewExpired,
            "opening a new view did not replace the old frozen view");
    Require(service.Query(observer, replacement.View->Id, query, cursor).Result == llm::Status::InvalidCursor,
            "cursor crossed into a replacement view");

    // Cursor identity is scoped by observer and view, even when query values match.
    const auto otherObserver = service.RegisterObserver(llm::Policy::FullAccess());
    const auto otherView = service.OpenView(otherObserver, FullScope());
    Require(service.Query(otherObserver, otherView.View->Id, query, cursor).Result == llm::Status::InvalidCursor,
            "cursor crossed observers or views");
    auto schemaQuery = query;
    schemaQuery.SchemaVersion = 1;
    const auto openAgain = service.OpenView(observer, FullScope());
    const auto pages = Deliver(service, observer, service.Query(observer, openAgain.View->Id, schemaQuery));
    Require(pages.Page && pages.Page->NextCursor != cursor,
            "replacement view reused a prior view cursor identity");
}

void BoundsCapabilitiesAndRevocation() {
    FakeSource source;
    llm::ViewService service(source);
    auto policy = llm::Policy{};
    policy.Topics[size_t(api::Topic::Materials)] = true;
    const auto observer = service.RegisterObserver(policy);
    const auto capabilities = service.DescribeCapabilities(observer);
    Require(capabilities.Topics.size() == 1 && capabilities.Topics.front().Topic == api::Topic::Materials
                && !capabilities.Facts,
            "capability catalog exposed denied topics or facts");
    Require(service.Query(observer, 0, Materials()).Result == llm::Status::ViewExpired,
            "query without an active view did not fail closed");
    const auto opened = service.OpenView(observer, FullScope());
    Require(opened.Result == llm::Status::Ok && opened.View, "restricted observer could not open a view");
    Require(service.Query(observer, opened.View->Id, {api::Topic::Runtime, FullScope()}).Result
                == llm::Status::PermissionDenied,
            "denied topic query disclosed data");
    Require(service.ReadFacts(observer, opened.View->Id, 0).Result == llm::Status::PermissionDenied,
            "denied fact read disclosed data");
    const auto pending = service.Query(observer, opened.View->Id, Materials());
    Require(pending.Result == llm::Status::Ok, "authorized query did not prepare");

    service.SetPolicy(observer, llm::Policy::FullAccess());
    Require(service.Query(observer, opened.View->Id, Materials()).Result == llm::Status::ViewRevoked
                && service.Deliver(observer, pending.Delivery).Result == llm::Status::ViewRevoked
                && service.Statistics().PendingDeliveries == 0 && service.Statistics().ActiveViews == 0,
            "permission change did not revoke the old view and pending result");
    const auto renewed = service.OpenView(observer, FullScope());
    Require(renewed.Result == llm::Status::Ok && renewed.View
                && renewed.View->Id != opened.View->Id && renewed.View->FactHighWatermark == 3,
            "new permissions did not permit a fresh view");

    Require(service.OpenView(observer, {{0, 0}, 65, 1}).Result == llm::Status::LimitExceeded,
            "scope edge limit was not enforced");
    Require(service.Query(observer, renewed.View->Id, Materials(FullScope(), 16)).Result
                == llm::Status::LimitExceeded,
            "more than sixteen fine pages were accepted");
    Require(service.Query(observer, renewed.View->Id, Materials({{0, 0}, 16, 16}, 257)).Result
                == llm::Status::LimitExceeded,
            "page larger than 256 cells was accepted");

    llm::Policy regional;
    regional.Topics[size_t(api::Topic::Materials)] = true;
    regional.Topics[size_t(api::Topic::AirVelocity)] = true;
    regional.Region = api::Rect{{0, 0}, 16, 16};
    const auto localObserver = service.RegisterObserver(regional);
    const auto localCaps = service.DescribeCapabilities(localObserver);
    Require(localCaps.Topics.size() == 2 && !localCaps.Facts,
            "regional policy exposed session-wide capability");
    Require(service.OpenView(localObserver, {{16, 0}, 1, 1}).Result == llm::Status::PermissionDenied,
            "regional policy opened a view outside its grant");
    const auto localView = service.OpenView(localObserver, *regional.Region);
    Require(localView.Result == llm::Status::Ok && localView.View,
            "regional observer could not open its authorized scope");
    bool rejectedGlobalFacts = false;
    try {
        auto invalid = regional;
        invalid.Facts = true;
        (void)service.RegisterObserver(invalid);
    } catch (const std::invalid_argument &) {
        rejectedGlobalFacts = true;
    }
    Require(rejectedGlobalFacts, "regional policy granted global facts");
    bool rejectedGlobalCounts = false;
    try {
        auto invalid = regional;
        invalid.Topics[size_t(api::Topic::Programs)] = true;
        (void)service.RegisterObserver(invalid);
    } catch (const std::invalid_argument &) {
        rejectedGlobalCounts = true;
    }
    Require(rejectedGlobalCounts, "regional policy granted session-wide program counts");

}

void ObserverCapacityEpochAndLease() {
    FakeSource source;
    llm::ViewService service(source);
    const auto first = service.RegisterObserver(llm::Policy::FullAccess());
    const auto second = service.RegisterObserver(llm::Policy::FullAccess());
    const auto third = service.RegisterObserver(llm::Policy::FullAccess());
    Require(first && second && third && !service.RegisterObserver(llm::Policy::FullAccess()),
            "observer registry exceeded its three-subject bound");
    const auto view = service.OpenView(first, FullScope());
    Require(view.View.has_value(), "observer view failed to open");

    // Query volume does not advance the host service-frame lease.
    for (int i = 0; i < 20; ++i) {
        auto response = Deliver(service, first, service.Query(first, view.View->Id, Materials()));
        Require(response.Page.has_value(), "repeated read unexpectedly expired its view");
    }
    Require(service.Statistics().ServiceFrame == 0,
            "query calls were counted as service frames");
    source.Waiting = true;
    source.Ready = false;
    for (uint32_t frame = 0; frame < llm::Limits::LeaseFrames; ++frame) service.AdvanceServiceFrame();
    Require(service.Query(first, view.View->Id, Materials()).Result == llm::Status::ViewExpired
                && service.Statistics().ServiceFrame == llm::Limits::LeaseFrames,
            "paused/budget-wait service frames did not expire the lease at 120 frames");

    source.Snapshot.Stamp.WorldEpoch += 1;
    source.Commit(source.Snapshot.Stamp.Revision + 1);
    const auto newView = service.OpenView(first, FullScope());
    Require(newView.Result == llm::Status::Ok && newView.View, "view could not reopen after epoch change");
    source.Snapshot.Stamp.WorldEpoch += 1;
    Require(service.Query(first, newView.View->Id, Materials()).Result == llm::Status::ViewExpired,
            "epoch replacement did not expire the active view");
}

void FixedFactsAndPendingDeliveryLimits() {
    FakeSource source;
    llm::ViewService service(source);
    const auto observer = service.RegisterObserver(llm::Policy::FullAccess());
    const auto view = service.OpenView(observer, FullScope());
    Require(view.View->FactHighWatermark == 3, "view did not freeze its fact high watermark");
    const auto beforePrepare = service.Statistics();
    auto pending = service.ReadFacts(observer, view.View->Id, 0, 2);
    Require(pending.Result == llm::Status::Ok, "facts were not prepared");
    const auto whilePending = service.Statistics();
    Require(service.Query(observer, view.View->Id, Materials()).Result == llm::Status::LimitExceeded,
            "second undelivered result was accepted");
    Require(whilePending.PendingDeliveries == 1 && whilePending.RetainedBytes > beforePrepare.RetainedBytes,
            "prepared result was not included in retained memory accounting");
    auto delivered = service.Deliver(observer, pending.Delivery);
    Require(delivered.Result == llm::Status::Ok && delivered.Facts && delivered.Facts->Facts.size() == 2
                && delivered.Facts->HighWatermark == 3 && delivered.FactView
                && delivered.FactView->Id == view.View->Id && source.LastReadHigh == 3,
            "fact read lost its view descriptor or crossed its fixed high watermark");
    Require(service.Statistics().RetainedBytes < whilePending.RetainedBytes,
            "delivery retained the prepared facts payload");
    source.Snapshot.FactHighWatermark = 10;
    auto fixed = Deliver(service, observer, service.ReadFacts(observer, view.View->Id, 2));
    Require(fixed.Facts && fixed.Facts->HighWatermark == 3 && fixed.Facts->Facts.size() == 1,
            "old view read facts beyond its fixed high watermark");
    source.FirstFact = 5;
    auto gap = Deliver(service, observer, service.ReadFacts(observer, view.View->Id, 0));
    Require(gap.Result == llm::Status::Gap && gap.Facts
                && gap.Facts->FirstAvailableSequence == 5,
            "fact ring loss was not reported as a gap");

    const auto before = service.Statistics();
    Require(service.CloseView(observer, view.View->Id) == llm::Status::Ok,
            "active view did not close");
    const auto after = service.Statistics();
    Require(after.ActiveViews == 0 && after.PendingDeliveries == 0
                && after.RetainedBytes < before.RetainedBytes,
            "closing a view retained snapshot or pending payload memory");
    for (int i = 0; i < 10; ++i) {
        const auto reopened = service.OpenView(observer, FullScope());
        Require(reopened.Result == llm::Status::Ok && reopened.View,
                "repeated open failed after close");
        Require(service.CloseView(observer, reopened.View->Id) == llm::Status::Ok,
                "repeated close failed");
    }
    Require(service.Statistics().ActiveViews == 0,
            "repeated open/close retained active view storage");
}

void PartialProjectionAndRegionalAggregation() {
    FakeSource source;
    source.Snapshot.Scope = {{0, 0}, 64, 64};
    source.Snapshot.Cells.resize(64 * 64);
    for (auto &cell : source.Snapshot.Cells) {
        cell.MaterialId = 1;
        cell.AirKind = api::AirSampleKind::Sampled;
        cell.AirVelocity = std::array<float, 2>{100, 100};
        cell.MaterialSampledAtRevision = 1;
        cell.WindSampledAtRevision = 1;
    }
    // Authorized 16x16 region has four samples; values outside it must not enter the average.
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; ++x)
            source.Snapshot.Cells[size_t(y) * 64 + x].AirVelocity = std::array<float, 2>{2, 4};
    source.Snapshot.Cells[0].MaterialId.reset();
    source.Snapshot.Cells[0].AirKind = api::AirSampleKind::NotResident;
    source.Snapshot.Cells[0].AirVelocity.reset();
    source.Snapshot.Cells[1].MaterialId = 2;
    source.Snapshot.Cells[1].AirKind = api::AirSampleKind::NotApplicable;
    source.Snapshot.Cells[1].AirVelocity.reset();
    source.Snapshot.ResidentCellCount = 4095;

    llm::Policy policy;
    policy.Topics[size_t(api::Topic::Materials)] = true;
    policy.Topics[size_t(api::Topic::AirVelocity)] = true;
    policy.Region = api::Rect{{0, 0}, 16, 16};
    llm::ViewService service(source);
    const auto observer = service.RegisterObserver(policy);
    const auto view = service.OpenView(observer, *policy.Region);
    Require(view.Result == llm::Status::Ok && view.View, "regional view failed to open");
    auto material = Deliver(service, observer, service.Query(observer, view.View->Id,
        Materials({{0, 0}, 2, 1}, 2)));
    Require(material.Page && material.Page->Availability == api::Availability::Partial
                && material.Page->Cells[0].MaterialId == std::nullopt
                && material.Page->Cells[1].MaterialId == 2,
            "partial material query treated unknown as air or lost known material");

    llm::QuerySpec wind;
    wind.Topic = api::Topic::AirVelocity;
    wind.Scope = *policy.Region;
    wind.ResolutionCells = 16;
    auto summary = Deliver(service, observer, service.Query(observer, view.View->Id, wind));
    Require(summary.Page && summary.Page->Availability == api::Availability::Partial
                && summary.Page->WindSummaries.size() == 1
                && summary.Page->WindSummaries[0].RequestedCells == 256
                && summary.Page->WindSummaries[0].ResidentCells == 255
                && summary.Page->WindSummaries[0].UnknownCells == 1
                && summary.Page->WindSummaries[0].AirSamples == 254
                && summary.Page->WindSummaries[0].MeanVelocityCellsPerSecond
                && *summary.Page->WindSummaries[0].MeanVelocityCellsPerSecond == std::array<double, 2>{2, 4},
            "local aggregation included hidden cells or mishandled partial unknown/non-air coverage");
}

void PendingSessionKeepsOnlyTheFrozenExternalView() {
    const auto directory = std::filesystem::temp_directory_path()
        / ("pixel-llm-view-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct RemoveDirectory {
        std::filesystem::path Path;
        ~RemoveDirectory() { std::filesystem::remove_all(Path); }
    } cleanup{directory};

    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    WorldStreamSettings settings;
    settings.Directory = directory;
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

    world.WaitForStreaming();
    world.WaitForStreaming();
    for (const auto coordinate : world.GetMap().ChunkCoordinates())
        if (!world.GetSimulationBounds().Contains(
                {coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge}))
            testing::WorldAccess::Map(world).Remove(coordinate);
    testing::SessionAccess::PublishFixtureChanges(session);

    llm::ViewService service(session);
    const auto observer = service.RegisterObserver(llm::Policy::FullAccess());
    const api::Rect scope{{64, 64}, 64, 64};
    const auto original = service.OpenView(observer, scope);
    Require(original.Result == llm::Status::Ok && original.View,
            "committed view could not open before the pending step");
    const auto originalRevision = original.View->Stamp.Revision;

    testing::WorldAccess::SaturateBudget(world);
    session.Tick(.1f, {1, false});
    Require(world.GetEnvironment().IsWaitingForBudget(), "fixture did not enter WaitingForBudget");
    llm::QuerySpec windQuery;
    windQuery.Topic = api::Topic::AirVelocity;
    windQuery.Scope = {{100, 100}, 1, 1};
    auto frozen = Deliver(service, observer, service.Query(observer, original.View->Id, windQuery));
    Require(frozen.Page && frozen.Page->View.Stamp.Revision == originalRevision,
            "old external view exposed the uncommitted pending step");
    Require(service.OpenView(observer, scope).Result == llm::Status::NotSampled,
            "pending step admitted a new external capture");
    const auto reread = service.Query(observer, original.View->Id, windQuery);
    Require(reread.Result == llm::Status::Ok,
            "failed pending capture discarded the prior frozen view");
    auto stillFrozen = Deliver(service, observer, reread);
    Require(stillFrozen.Page && stillFrozen.Page->View.Stamp.Revision == originalRevision,
            "failed pending capture discarded the prior frozen view");

    world.SetPublicationBytes(0);
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(.2f, {-1, true});
    Require(!world.GetEnvironment().IsWaitingForBudget(), "pending step did not recover after budget release");
    const auto committed = service.OpenView(observer, scope);
    Require(committed.Result == llm::Status::Ok && committed.View
                && committed.View->Stamp.Revision == session.CurrentCommit().Revision
                && committed.View->Stamp.Revision > originalRevision,
            "new external view did not capture the recovered complete commit");
}

void RealSessionMiningEpochAndNoLoading() {
    GameSession session;
    llm::ViewService service(session);
    const auto observer = service.RegisterObserver(llm::Policy::FullAccess());
    const api::Rect scope{{504, 504}, 32, 16};
    const auto oldView = service.OpenView(observer, scope);
    Require(oldView.Result == llm::Status::Ok && oldView.View,
            "GameSession capture slot 3 did not open a real view");

    llm::QuerySpec material;
    material.Topic = api::Topic::Materials;
    material.Scope = {{520, 512}, 1, 1};
    auto oldCell = Deliver(service, observer, service.Query(observer, oldView.View->Id, material));
    Require(oldCell.Page && oldCell.Page->Cells.front().MaterialId == uint32_t(CellTypeId::Dirt),
            "real session snapshot did not report the initial Dirt cell");
    const auto mined = session.Execute(NativeCaller::Driver,
        api::MineRegion{{520, 512}, uint32_t(CellTypeId::Dirt)});
    Require(mined.Applied() && mined.Facts.Count == 1, "real session mine did not commit its fact");
    auto frozen = Deliver(service, observer, service.Query(observer, oldView.View->Id, material));
    Require(frozen.Page && frozen.Page->View.Stamp.Revision == oldView.View->Stamp.Revision
                && frozen.Page->Cells.front().MaterialId == uint32_t(CellTypeId::Dirt),
            "old session view changed after mining");
    const auto newView = service.OpenView(observer, scope);
    material.Scope = {{520, 512}, 1, 1};
    auto newCell = Deliver(service, observer, service.Query(observer, newView.View->Id, material));
    Require(newCell.Page && newCell.Page->Cells.front().MaterialId == uint32_t(CellTypeId::Air)
                && newCell.Page->View.Stamp.Revision == session.CurrentCommit().Revision,
            "new session view did not observe committed mining");

    // Keep room below the 49-bit chunk-coordinate limit when ChunkEdge is 64.
    const int64_t far = (int64_t{1} << 55) - 8192;
    const auto chunksBefore = session.GetWorld().GetMap().ChunkCount();
    const auto farView = service.OpenView(observer, {{far, -far}, 8, 8});
    Require(farView.Result == llm::Status::Ok && farView.View
                && session.GetWorld().GetMap().ChunkCount() == chunksBefore,
            "external capture loaded world chunks for a remote scope");
    material.Scope = {{far, -far}, 1, 1};
    auto unknown = Deliver(service, observer, service.Query(observer, farView.View->Id, material));
    Require(unknown.Page && unknown.Page->Availability == api::Availability::Partial
                && !unknown.Page->Cells.front().MaterialId
                && session.GetWorld().GetMap().ChunkCount() == chunksBefore,
            "remote query converted unknown to Air or loaded the world");

    const auto epoch = session.WorldEpoch();
    session.Control(NativeCaller::Driver, api::ResetWorld{});
    Require(session.WorldEpoch() != epoch
                && service.Query(observer, farView.View->Id, material).Result == llm::Status::ViewExpired,
            "world reset failed to expire external views by epoch");
}

void ExtremeCoordinatesRemainValidForSummary() {
    for (const int64_t edge : {INT64_MIN + 16, INT64_MAX - 20}) {
        const api::Rect scope{{edge, edge}, 1, 1};
        FakeSource source(scope);
        llm::ViewService service(source);
        const auto observer = service.RegisterObserver(llm::Policy::FullAccess());
        const auto opened = service.OpenView(observer, scope);
        Require(opened.View.has_value(), "valid extreme rectangle could not open");
        const auto result = Deliver(service, observer,
            service.Query(observer, opened.View->Id, {api::Topic::AirVelocity, scope, 16}));
        Require(result.Page && result.Page->WindSummaries.size() == 1
            && result.Page->WindSummaries[0].Scope == scope, "extreme summary changed its exact coordinates");
    }
}

void CursorAndByteBudgets() {
    FakeSource source;
    llm::ViewService service(source);
    const auto idleBytes = service.Statistics().RetainedBytes;
    const auto observer = service.RegisterObserver(llm::Policy::FullAccess());
    const auto opened = service.OpenView(observer, FullScope());
    Require(opened.View.has_value(), "bounded view did not open");
    llm::CursorId originalCursor = 0;
    for (uint32_t i = 0; i < llm::Limits::MaximumCursors; ++i) {
        const auto query = Materials({{int64_t(i % 49), int64_t(i / 49)}, 16, 16}, 128);
        const auto result = Deliver(service, observer, service.Query(observer, opened.View->Id, query));
        Require(result.Page && result.Page->NextCursor, "bounded query did not allocate its cursor");
        if (i == 0) originalCursor = result.Page->NextCursor;
    }
    Require(service.Query(observer, opened.View->Id, Materials({{15, 1}, 16, 16}, 128)).Result
        == llm::Status::LimitExceeded, "cursor registry grew past its bounded capacity");
    const auto replay = Deliver(service, observer,
        service.Query(observer, opened.View->Id, Materials({{0, 0}, 16, 16}, 128)));
    Require(replay.Page && replay.Page->NextCursor == originalCursor,
        "replaying a page consumed another cursor or invalidated the existing cursor");

    source.Snapshot.Stamp.WorldId.assign(llm::Limits::MaximumViewBytes, 'x');
    source.Snapshot.Gameplay.Stamp = source.Snapshot.Stamp;
    Require(service.OpenView(observer, FullScope()).Result == llm::Status::LimitExceeded,
        "oversized snapshot was retained");
    Require(service.Query(observer, opened.View->Id, Materials()).Result == llm::Status::Ok,
        "rejected oversized replacement destroyed its old view");
    // Close also discards the one undelivered response from the preceding check.
    Require(service.CloseView(observer, opened.View->Id) == llm::Status::Ok, "budget fixture close failed");
    Require(service.Statistics().RetainedBytes == idleBytes, "closed view retained dynamic allocations");

    // Find the actual metadata boundary rather than pinning a magic WorldId
    // length to the current set of owned domain DTOs and their fixed overhead.
    size_t acceptedBytes = 1, rejectedBytes = llm::Limits::MaximumViewBytes;
    while (rejectedBytes - acceptedBytes > 1) {
        const size_t candidate = acceptedBytes + (rejectedBytes - acceptedBytes) / 2;
        source.Snapshot.Stamp.WorldId = std::string(candidate, 'w');
        source.Snapshot.Gameplay.Stamp = source.Snapshot.Stamp;
        const auto attempt = service.OpenView(observer, FullScope());
        if (attempt.Result == llm::Status::Ok) acceptedBytes = candidate;
        else {
            Require(attempt.Result == llm::Status::LimitExceeded, "metadata budget search failed for an unrelated reason");
            rejectedBytes = candidate;
        }
    }
    source.Snapshot.Stamp.WorldId = std::string(acceptedBytes, 'w');
    source.Snapshot.Gameplay.Stamp = source.Snapshot.Stamp;
    const auto large = service.OpenView(observer, FullScope());
    Require(large.View.has_value(), "valid large metadata snapshot exceeded its budget");
    Require(service.Statistics().RetainedBytes > idleBytes + llm::Limits::MaximumViewBytes / 2,
        "metadata fixture did not approach the real retained byte budget");
    std::cout << "view metadata boundary: " << acceptedBytes << " bytes; retained view bytes: "
              << service.Statistics().RetainedBytes - idleBytes << '\n';
    Require(service.Query(observer, large.View->Id, Materials()).Result == llm::Status::LimitExceeded
        && service.ReadFacts(observer, large.View->Id, 0).Result == llm::Status::LimitExceeded,
        "pending results exceeded the combined snapshot and response byte budget");
    Require(service.Statistics().PendingDeliveries == 0
        && service.Statistics().RetainedBytes < idleBytes + llm::Limits::MaximumViewBytes,
        "rejected response remained retained");
}

void PopulateGameplay(FakeSource &source) {
    auto &values = source.Snapshot.Gameplay;
    values.Inventories = {
        {101, 1, 11, 0, {{1, 1}, {0, 0}}, 20, 6, {{41, 1, 6, 2}}},
        {102, 1, 22, 12, {{2, 1}, {0, 0}}, 20, 9, {{42, 1, 9, 0}}},
        {103, 1, 11, 13, {{3, 1}, {0, 0}}, 20, 4, {{43, 2, 4, 1}}},
        {104, 1, 11, 14, {{64, 1}, {0, 0}}, 20, 7, {{44, 1, 7, 0}}},
        {105, 1, 11, 15, {{20, 1}, {0, 0}}, 20, 8, {{45, 1, 8, 0}}}
    };
    values.Devices = {
        {{12, 1}, ForceEmitterType, 22, {2, 1}, true, DeviceState::Running, 202, 4, 9},
        {{13, 1}, ForceEmitterType, 11, {3, 1}, true, DeviceState::NeedsFuel, 203, 5, 4},
        {{14, 1}, ForceEmitterType, 11, {64, 1}, false, DeviceState::Stopped, 204, 6, 7}
    };
    values.Processes = {
        {202, 12, {42, 5}, api::CarrierKind::Device, {{2, 1}, {0, 0}}, api::ProcessPhase::Ready, false, 5, 9},
        {203, 13, {43, 6}, api::CarrierKind::Device, {{3, 1}, {0, 0}}, api::ProcessPhase::Sleeping, true, 8, 11},
        {204, 14, {44, 7}, api::CarrierKind::NaturalSource, {{64, 1}, {0, 0}}, api::ProcessPhase::Awaiting, false, 2, 12}
    };
    values.FieldSources = {
        {301, 202, 202, 12, {{2, 1}, {.5, .5}}, 3.5, {1.25, -.5}, 12, false, true},
        {302, 203, 203, 13, {{3, 1}, {.5, .5}}, 4.5, {2.25, -.75}, 14, true, false},
        {303, 204, 204, 14, {{64, 1}, {.5, .5}}, 5.5, {3.25, -1.5}, 16, false, false}
    };
}

llm::QuerySpec GameplayQuery(api::Topic topic, uint32_t pageSize = 1) {
    return {topic, {{0, 0}, 8, 8}, 1, pageSize};
}

void InventoryOwnershipAndEpochPrivacy() {
    FakeSource source;
    PopulateGameplay(source);
    llm::Policy policy;
    policy.Topics[size_t(api::Topic::Items)] = true;
    policy.Topics[size_t(api::Topic::Devices)] = true;
    llm::ViewService service(source);
    const auto publicObserver = service.RegisterObserver(policy);
    const auto publicView = service.OpenView(publicObserver, FullScope());
    auto items = Deliver(service, publicObserver,
        service.Query(publicObserver, publicView.View->Id, GameplayQuery(api::Topic::Items)));
    Require(items.Page && items.Page->Inventories.empty() && !items.Page->TotalRecords && !items.Page->NextCursor,
        "items topic grant disclosed private inventory values or cardinality");
    auto devices = Deliver(service, publicObserver,
        service.Query(publicObserver, publicView.View->Id, GameplayQuery(api::Topic::Devices, 16)));
    Require(devices.Page && devices.Page->Devices.size() == 2 && !devices.Page->Devices[0].Fuel
        && !devices.Page->Devices[1].Fuel && devices.Page->Devices[1].State == DeviceState::NeedsFuel,
        "public device status lost its state or disclosed private fuel");

    policy.InventoryOwner = llm::InventoryGrant{7, 11};
    const auto owner = service.RegisterObserver(policy);
    const auto opened = service.OpenView(owner, FullScope());
    const auto query = GameplayQuery(api::Topic::Items);
    auto first = Deliver(service, owner, service.Query(owner, opened.View->Id, query));
    Require(first.Page && first.Page->TotalRecords == 2 && first.Page->Inventories.size() == 1
        && first.Page->Inventories[0].ContainerId == 101 && first.Page->Inventories[0].Stacks[0].Reserved == 2
        && first.Page->NextCursor,
        "owner inventory paging included another owner or out-of-scope containers");
    const auto cursor = first.Page->NextCursor;
    auto second = Deliver(service, owner, service.Query(owner, opened.View->Id, query, cursor));
    Require(second.Page && second.Page->Inventories[0].ContainerId == 103 && !second.Page->NextCursor,
        "container pagination omitted or split an authorized inventory");
    auto ownedDevices = Deliver(service, owner,
        service.Query(owner, opened.View->Id, GameplayQuery(api::Topic::Devices, 16)));
    Require(ownedDevices.Page && !ownedDevices.Page->Devices[0].Fuel && ownedDevices.Page->Devices[1].Fuel == 4,
        "device fuel authorization disagreed with inventory ownership");
    Require(service.ReadFacts(owner, opened.View->Id, 0).Result == llm::Status::PermissionDenied,
        "owner-only observer obtained full-session device facts");
    bool rejectedFacts = false;
    try {
        auto invalid = policy;
        invalid.Facts = true;
        service.RegisterObserver(invalid);
    } catch (const std::invalid_argument &) { rejectedFacts = true; }
    Require(rejectedFacts, "session facts did not require an explicit full private grant");

    auto prepared = service.Query(owner, opened.View->Id, query, cursor);
    Require(prepared.Result == llm::Status::Ok, "private inventory delivery was not prepared");
    policy.InventoryOwner.reset();
    service.SetPolicy(owner, policy);
    Require(service.Deliver(owner, prepared.Delivery).Result == llm::Status::ViewRevoked
        && service.Query(owner, opened.View->Id, query, cursor).Result == llm::Status::ViewRevoked,
        "inventory grant revocation preserved a prepared value or cursor");
    policy.InventoryOwner = llm::InventoryGrant{7, 11};
    service.SetPolicy(owner, policy);
    source.Snapshot.Stamp.WorldEpoch = 8;
    source.Commit(2);
    const auto changedEpoch = service.OpenView(owner, FullScope());
    auto staleGrant = Deliver(service, owner, service.Query(owner, changedEpoch.View->Id, query));
    Require(staleGrant.Page && staleGrant.Page->Inventories.empty() && !staleGrant.Page->TotalRecords,
        "owner actor identity reused across epochs inherited a private inventory grant");
    auto staleFuel = Deliver(service, owner,
        service.Query(owner, changedEpoch.View->Id, GameplayQuery(api::Topic::Devices, 16)));
    Require(staleFuel.Page && !staleFuel.Page->Devices[1].Fuel, "stale-epoch grant exposed device fuel");

    service.SetPolicy(owner, llm::Policy::FullAccess());
    const auto all = service.OpenView(owner, FullScope());
    auto allInventories = Deliver(service, owner,
        service.Query(owner, all.View->Id, GameplayQuery(api::Topic::Items, 16)));
    Require(allInventories.Page && allInventories.Page->TotalRecords == 3 && allInventories.Page->Inventories.size() == 3,
        "FullAccess did not grant all in-scope inventories");
    const auto factResponse = service.ReadFacts(owner, all.View->Id, 0);
    Require(factResponse.Result == llm::Status::Ok, "full private observer could not read session facts");
    auto restricted = llm::Policy::FullAccess();
    restricted.Facts = false;
    restricted.AllInventories = false;
    service.SetPolicy(owner, restricted);
    Require(service.Deliver(owner, factResponse.Delivery).Result == llm::Status::ViewRevoked,
        "private fact delivery survived full inventory grant revocation");
}

void GameplayValuesRemainFrozenAcrossCommits() {
    FakeSource source;
    PopulateGameplay(source);
    llm::ViewService service(source);
    const auto observer = service.RegisterObserver(llm::Policy::FullAccess());
    const auto opened = service.OpenView(observer, FullScope());
    const auto itemQuery = GameplayQuery(api::Topic::Items);
    auto items = Deliver(service, observer, service.Query(observer, opened.View->Id, itemQuery));
    const auto cursor = items.Page->NextCursor;
    auto devicePage = Deliver(service, observer,
        service.Query(observer, opened.View->Id, GameplayQuery(api::Topic::Devices)));
    const auto deviceCursor = devicePage.Page->NextCursor;
    auto processPage = Deliver(service, observer,
        service.Query(observer, opened.View->Id, GameplayQuery(api::Topic::Programs)));
    const auto processCursor = processPage.Page->NextCursor;
    auto fieldPage = Deliver(service, observer,
        service.Query(observer, opened.View->Id, GameplayQuery(api::Topic::FieldSources)));
    const auto fieldCursor = fieldPage.Page->NextCursor;
    Require(items.Page->TotalRecords == 3 && devicePage.Page->TotalRecords == 2
        && processPage.Page->TotalRecords == 2 && fieldPage.Page->TotalRecords == 2,
        "spatial gameplay filtering did not precede counts and paging");
    auto boundary = GameplayQuery(api::Topic::FieldSources, 16);
    boundary.Scope.Width = 2;
    auto overlappingFields = Deliver(service, observer, service.Query(observer, opened.View->Id, boundary));
    Require(overlappingFields.Page->FieldSources.empty() && !overlappingFields.Page->TotalRecords,
        "field radius overlap bypassed center-based query scope filtering");
    source.Snapshot.Gameplay.Inventories.erase(source.Snapshot.Gameplay.Inventories.begin() + 1);
    source.Snapshot.Gameplay.Devices[1].State = DeviceState::Stopped;
    source.Snapshot.Gameplay.Devices[1].Fuel = 0;
    source.Snapshot.Gameplay.Processes[1].Phase = api::ProcessPhase::Ended;
    source.Snapshot.Gameplay.FieldSources.clear();
    source.Commit(2, 10);
    auto frozenItems = Deliver(service, observer, service.Query(observer, opened.View->Id, itemQuery, cursor));
    auto frozenDevices = Deliver(service, observer,
        service.Query(observer, opened.View->Id, GameplayQuery(api::Topic::Devices), deviceCursor));
    auto frozenProcesses = Deliver(service, observer,
        service.Query(observer, opened.View->Id, GameplayQuery(api::Topic::Programs), processCursor));
    auto frozenFields = Deliver(service, observer,
        service.Query(observer, opened.View->Id, GameplayQuery(api::Topic::FieldSources), fieldCursor));
    Require(frozenItems.Page->Inventories[0].ContainerId == 102 && frozenItems.Page->TotalRecords == 3
        && frozenDevices.Page->Devices[0].State == DeviceState::NeedsFuel && frozenDevices.Page->Devices[0].Fuel == 4
        && frozenProcesses.Page->Processes[0].Phase == api::ProcessPhase::Sleeping
        && frozenFields.Page->FieldSources[0].Handle == 302 && frozenFields.Page->FieldSources[0].OwnerInstance == 203
        && frozenFields.Page->FieldSources[0].AccelerationCellsPerSecondSquared == std::array<double, 2>{2.25, -.75}
        && frozenFields.Page->View.Stamp.Revision == 1 && frozenItems.Page->View.Stamp.Revision == 1,
        "paging an old view mixed deleted or changed gameplay values from another commit");
    const auto fresh = service.OpenView(observer, FullScope());
    auto newItems = Deliver(service, observer,
        service.Query(observer, fresh.View->Id, GameplayQuery(api::Topic::Items, 16)));
    auto newDevices = Deliver(service, observer,
        service.Query(observer, fresh.View->Id, GameplayQuery(api::Topic::Devices, 16)));
    auto newProcesses = Deliver(service, observer,
        service.Query(observer, fresh.View->Id, GameplayQuery(api::Topic::Programs, 16)));
    auto newFields = Deliver(service, observer,
        service.Query(observer, fresh.View->Id, GameplayQuery(api::Topic::FieldSources, 16)));
    Require(newItems.Page->TotalRecords == 2 && newItems.Page->View.Stamp.Revision == 2
        && newDevices.Page->Devices[1].State == DeviceState::Stopped && newDevices.Page->Devices[1].Fuel == 0
        && newProcesses.Page->Processes[1].Phase == api::ProcessPhase::Ended
        && newFields.Page->FieldSources.empty() && !newFields.Page->TotalRecords,
        "new gameplay view did not observe the committed deletion and state changes");
    Require(service.Query(observer, fresh.View->Id, itemQuery, cursor).Result == llm::Status::InvalidCursor,
        "inventory cursor was reused across committed views");
}

void PrivateValuesAreRemovedBeforeRetention() {
    FakeSource source;
    PopulateGameplay(source);
    llm::Policy policy;
    policy.Topics[size_t(api::Topic::Items)] = true;
    policy.Topics[size_t(api::Topic::Devices)] = true;
    llm::ViewService service(source);
    const auto observer = service.RegisterObserver(policy);
    Require(service.OpenView(observer, FullScope()).View.has_value(), "public fixture did not open");
    const auto before = service.Statistics().RetainedBytes;
    source.Snapshot.Gameplay.Inventories[0].Stacks.resize(100000);
    Require(service.OpenView(observer, FullScope()).View.has_value()
        && service.Statistics().RetainedBytes == before,
        "private inventory allocation was retained or influenced the external view byte budget");
    source.Snapshot.Gameplay.Stamp.Revision++;
    bool rejected = false;
    try { service.OpenView(observer, FullScope()); }
    catch (const std::logic_error &) { rejected = true; }
    Require(rejected, "mixed-commit gameplay capture did not fail fast");
}
} // namespace

int main() {
    try {
        FrozenViewsAndCursorBinding();
        InventoryOwnershipAndEpochPrivacy();
        GameplayValuesRemainFrozenAcrossCommits();
        PrivateValuesAreRemovedBeforeRetention();
        BoundsCapabilitiesAndRevocation();
        ObserverCapacityEpochAndLease();
        FixedFactsAndPendingDeliveryLimits();
        PartialProjectionAndRegionalAggregation();
        PendingSessionKeepsOnlyTheFrozenExternalView();
        RealSessionMiningEpochAndNoLoading();
        ExtremeCoordinatesRemainValidForSummary();
        CursorAndByteBudgets();
        std::cout << "LLM view contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
