#include "ViewService.h"
#include "QueryProjection.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace pixel::llm {
const char *StatusName(Status status) {
    switch (status) {
    case Status::Ok: return "Ok";
    case Status::PermissionDenied: return "PermissionDenied";
    case Status::ViewRevoked: return "ViewRevoked";
    case Status::ViewExpired: return "ViewExpired";
    case Status::InvalidArgument: return "InvalidArgument";
    case Status::InvalidCursor: return "InvalidCursor";
    case Status::LimitExceeded: return "LimitExceeded";
    case Status::Unsupported: return "Unsupported";
    case Status::NotSampled: return "NotSampled";
    case Status::SessionUnavailable: return "SessionUnavailable";
    case Status::Gap: return "Gap";
    }
    throw std::logic_error("unknown view status");
}

const char *TopicName(api::Topic topic) {
    switch (topic) {
    case api::Topic::Materials: return "materials";
    case api::Topic::AirVelocity: return "air_velocity";
    case api::Topic::Player: return "player";
    case api::Topic::Programs: return "programs";
    case api::Topic::Runtime: return "runtime";
    case api::Topic::Temperature: return "temperature";
    case api::Topic::Ecology: return "ecology";
    case api::Topic::Items: return "items";
    case api::Topic::Devices: return "devices";
    case api::Topic::Knowledge: return "knowledge";
    case api::Topic::FieldSources: return "field_sources";
    case api::Topic::Count: break;
    }
    throw std::logic_error("unknown observation topic");
}

Policy Policy::FullAccess() {
    Policy policy;
    policy.Topics.fill(true);
    policy.Facts = true;
    policy.AllInventories = true;
    return policy;
}

uint64_t QueryPage::RetainedBytes() const {
    uint64_t bytes = sizeof(*this) + View.Stamp.WorldId.capacity() + 1
        + Cells.capacity() * sizeof(CellSample) + WindSummaries.capacity() * sizeof(api::WindSummary)
        + Projectiles.capacity() * sizeof(api::ProjectileObservation)
        + Inventories.capacity() * sizeof(api::InventoryObservation)
        + Devices.capacity() * sizeof(api::DeviceObservation)
        + Processes.capacity() * sizeof(api::ProcessObservation)
        + FieldSources.capacity() * sizeof(api::FieldSourceObservation);
    for (const auto &inventory : Inventories) bytes += inventory.RetainedBytes() - sizeof(inventory);
    return bytes;
}

uint64_t Response::RetainedBytes() const {
    uint64_t bytes = sizeof(*this);
    if (Page) bytes += Page->RetainedBytes() - sizeof(QueryPage);
    if (FactView) bytes += FactView->Stamp.WorldId.capacity() + 1;
    if (Facts) {
        bytes += Facts->Facts.capacity() * sizeof(api::DomainFact);
        for (const auto &fact : Facts->Facts) bytes += fact.CommittedAt.WorldId.capacity() + 1;
    }
    return bytes;
}

namespace {
void ValidatePolicy(const Policy &policy) {
    if (policy.Facts && !policy.AllInventories)
        throw std::invalid_argument("session facts require an explicit full inventory grant");
    if (policy.InventoryOwner && (!policy.InventoryOwner->WorldEpoch || !policy.InventoryOwner->OwnerActor))
        throw std::invalid_argument("inventory owner grant requires a nonzero epoch and actor");
    if (policy.Region && (!ValidRect(*policy.Region) || policy.Facts
        || policy.Topics[size_t(api::Topic::Programs)] || policy.Topics[size_t(api::Topic::Runtime)]))
        throw std::invalid_argument("spatial observer cannot grant session-wide summaries or facts");
}

bool InventoryAllowed(const Policy &policy, uint64_t epoch, uint64_t owner) {
    return policy.AllInventories || (policy.InventoryOwner
        && policy.InventoryOwner->WorldEpoch == epoch && policy.InventoryOwner->OwnerActor == owner);
}

void AuthorizeGameplay(api::ObservationSnapshot &snapshot, const Policy &policy) {
    auto &gameplay = snapshot.Gameplay;
    if (gameplay.Stamp != snapshot.Stamp)
        throw std::logic_error("gameplay observations do not belong to the snapshot commit");
    // Rebuild rather than retaining unauthorized container allocations in the
    // external view. Topic access alone never grants inventory values.
    std::vector<api::InventoryObservation> inventories;
    if (policy.Topics[size_t(api::Topic::Items)]) {
        for (auto &inventory : gameplay.Inventories)
            if (InventoryAllowed(policy, snapshot.Stamp.WorldEpoch, inventory.OwnerActor)
                && PositionInside(snapshot.Scope, inventory.Location))
                inventories.push_back(std::move(inventory));
    }
    gameplay.Inventories = std::move(inventories);
    for (auto &device : gameplay.Devices)
        if (!InventoryAllowed(policy, snapshot.Stamp.WorldEpoch, device.OwnerActor)) device.Fuel.reset();
}

uint64_t Allocate(uint64_t &next) {
    if (next == UINT64_MAX) throw std::overflow_error("view service identity exhausted");
    return next++;
}

Status CaptureStatus(api::ObservationStatus status) {
    switch (status) {
    case api::ObservationStatus::Available: return Status::Ok;
    case api::ObservationStatus::NotSampled: return Status::NotSampled;
    case api::ObservationStatus::SessionUnavailable: return Status::SessionUnavailable;
    case api::ObservationStatus::InvalidArgument: return Status::InvalidArgument;
    case api::ObservationStatus::LimitExceeded: return Status::LimitExceeded;
    }
    throw std::logic_error("unknown observation capture status");
}
} // namespace

struct ViewService::State {
    struct Cursor {
        CursorId Id = 0;
        QuerySpec Query;
        uint32_t Offset = 0;
    };
    struct View {
        ViewDescriptor Descriptor;
        uint64_t AuthorizationVersion = 0;
        api::ObservationSnapshot Snapshot;
        std::array<Cursor, Limits::MaximumCursors> Cursors;
        uint32_t CursorCount = 0;
        uint64_t RetainedBytes() const {
            return sizeof(*this) + Descriptor.Stamp.WorldId.capacity() + 1
                + Snapshot.RetainedBytes() - sizeof(api::ObservationSnapshot);
        }
    };
    struct Observer {
        ObserverId Id = 0;
        Policy Grant;
        uint64_t AuthorizationVersion = 1;
        ViewId RevokedView = 0;
        std::unique_ptr<View> Active;
        // Keep just the last delivery identity after invalidation, not its data.
        DeliveryId Delivery = 0;
        ViewId DeliveryView = 0;
        std::optional<Response> Pending;
        uint64_t RetainedBytes() const {
            return sizeof(*this) + (Active ? Active->RetainedBytes() : 0)
                + (Pending ? Pending->RetainedBytes() - sizeof(Response) : 0);
        }
        void Release() { Active.reset(); Pending.reset(); }
    };

    api::ObservationSource &Source;
    std::array<Observer, Limits::MaximumObservers> Observers;
    uint64_t Frame = 0, NextObserver = 1, NextView = 1, NextCursor = 1, NextDelivery = 1;
    explicit State(api::ObservationSource &source) : Source(source) {}

    Observer *Find(ObserverId id) {
        for (auto &observer : Observers) if (id && observer.Id == id) return &observer;
        return nullptr;
    }
    Status Check(Observer &observer, ViewId id) {
        if (id && id == observer.RevokedView) return Status::ViewRevoked;
        if (!observer.Active || observer.Active->Descriptor.Id != id) return Status::ViewExpired;
        auto &view = *observer.Active;
        if (view.AuthorizationVersion != observer.AuthorizationVersion)
            throw std::logic_error("stale authorization retained by view service");
        if (Frame >= view.Descriptor.ExpiresAtServiceFrame
            || Source.ObservationService().LastCommitted.WorldEpoch != view.Descriptor.Stamp.WorldEpoch) {
            observer.Release();
            return Status::ViewExpired;
        }
        return Status::Ok;
    }
    PreparedResponse Prepare(Observer &observer, Response response) {
        const auto prospective = observer.RetainedBytes() + response.RetainedBytes() - sizeof(Response);
        if (prospective > Limits::MaximumViewBytes) return {Status::LimitExceeded};
        observer.Delivery = Allocate(NextDelivery);
        observer.DeliveryView = observer.Active->Descriptor.Id;
        observer.Pending = std::move(response);
        return {Status::Ok, observer.Delivery};
    }
};

ViewService::ViewService(api::ObservationSource &source) : Data(std::make_unique<State>(source)) {}
ViewService::~ViewService() = default;

ObserverId ViewService::RegisterObserver(Policy policy) {
    ValidatePolicy(policy);
    for (auto &observer : Data->Observers) {
        if (observer.Id) continue;
        observer = {};
        observer.Id = Allocate(Data->NextObserver);
        observer.Grant = std::move(policy);
        return observer.Id;
    }
    return 0; // The host can advertise/handle capacity without allocating a registry entry.
}

void ViewService::SetPolicy(ObserverId id, Policy policy) {
    ValidatePolicy(policy);
    auto *observer = Data->Find(id);
    if (!observer) throw std::invalid_argument("unknown host observer");
    if (observer->AuthorizationVersion == UINT64_MAX) throw std::overflow_error("observer authorization exhausted");
    ++observer->AuthorizationVersion;
    if (observer->Active) observer->RevokedView = observer->Active->Descriptor.Id;
    observer->Release();
    observer->Grant = std::move(policy);
}

void ViewService::RemoveObserver(ObserverId id) {
    auto *observer = Data->Find(id);
    if (!observer) throw std::invalid_argument("unknown host observer");
    *observer = {};
}

void ViewService::AdvanceServiceFrame() {
    if (Data->Frame == UINT64_MAX) throw std::overflow_error("service frame exhausted");
    ++Data->Frame;
    // Release expired storage even when no query is submitted. Tick and frame
    // counters are intentionally unrelated, including during budget waits.
    for (auto &observer : Data->Observers)
        if (observer.Active) Data->Check(observer, observer.Active->Descriptor.Id);
}

Capabilities ViewService::DescribeCapabilities(ObserverId id) const {
    const auto *observer = Data->Find(id);
    if (!observer) return {Status::PermissionDenied};
    Capabilities result;
    result.Region = observer->Grant.Region;
    result.Facts = observer->Grant.Facts;
    result.AllInventories = observer->Grant.AllInventories;
    result.InventoryOwner = observer->Grant.InventoryOwner;
    for (size_t i = 0; i < observer->Grant.Topics.size(); ++i)
        if (observer->Grant.Topics[i]) result.Topics.push_back({api::Topic(i), api::TopicSupport(api::Topic(i))});
    return result;
}

OpenResult ViewService::OpenView(ObserverId id, api::Rect scope) {
    auto *observer = Data->Find(id);
    if (!observer || (!observer->Grant.Facts
        && std::none_of(observer->Grant.Topics.begin(), observer->Grant.Topics.end(), [](bool value) { return value; })))
        return {Status::PermissionDenied};
    if (!ValidRect(scope)) return {Status::InvalidArgument};
    if (scope.Width > Limits::MaximumEdge || scope.Height > Limits::MaximumEdge) return {Status::LimitExceeded};
    if (observer->Grant.Region && !Contains(*observer->Grant.Region, scope)) return {Status::PermissionDenied};
    auto capture = Data->Source.CaptureObservation(scope);
    if (capture.Status != api::ObservationStatus::Available) return {CaptureStatus(capture.Status)};
    if (!capture.Snapshot || capture.Snapshot->Scope != scope
        || capture.Snapshot->Cells.size() != size_t(scope.Width) * scope.Height)
        throw std::logic_error("observation source violated bounded snapshot contract");
    AuthorizeGameplay(*capture.Snapshot, observer->Grant);
    if (capture.Snapshot->RetainedBytes() + sizeof(State::View) + sizeof(State::Observer)
        + capture.Snapshot->Stamp.WorldId.capacity() + 1 > Limits::MaximumViewBytes)
        return {Status::LimitExceeded};
    // A failed capture (especially a prepared simulation step) preserves the
    // previous view. Replace only after validating the new owned snapshot.
    observer->Release();
    auto view = std::make_unique<State::View>();
    view->Snapshot = std::move(*capture.Snapshot);
    view->AuthorizationVersion = observer->AuthorizationVersion;
    if (Data->Frame > UINT64_MAX - Limits::LeaseFrames) throw std::overflow_error("view lease exhausted");
    view->Descriptor = {Allocate(Data->NextView), 1, view->Snapshot.Stamp, scope, Data->Frame + Limits::LeaseFrames, {}};
    if (observer->Grant.Facts) view->Descriptor.FactHighWatermark = view->Snapshot.FactHighWatermark;
    auto descriptor = view->Descriptor;
    observer->Active = std::move(view);
    return {Status::Ok, std::move(descriptor)};
}

PreparedResponse ViewService::Query(ObserverId id, ViewId viewId, QuerySpec query, CursorId cursorId) {
    auto *observer = Data->Find(id);
    if (!observer) return {Status::PermissionDenied};
    if (const auto status = Data->Check(*observer, viewId); status != Status::Ok) return {status};
    if (size_t(query.Topic) >= observer->Grant.Topics.size() || !observer->Grant.Topics[size_t(query.Topic)])
        return {Status::PermissionDenied};
    if (observer->Pending) return {Status::LimitExceeded};
    if (query.SchemaVersion != 1 || !ValidRect(query.Scope) || query.PageSize == 0) return {Status::InvalidArgument};
    if (query.PageSize > Limits::MaximumPageCells) return {Status::LimitExceeded};
    const auto &snapshot = observer->Active->Snapshot;
    if (!Contains(snapshot.Scope, query.Scope)) return {Status::PermissionDenied};
    if (query.ResolutionCells != 1 && !(query.Topic == api::Topic::AirVelocity && query.ResolutionCells == 16))
        return {Status::Unsupported};
    const uint32_t total = QueryRecordCount(snapshot, query);
    if ((uint64_t(total) + query.PageSize - 1) / query.PageSize > Limits::MaximumPages)
        return {Status::LimitExceeded};
    auto &view = *observer->Active;
    uint32_t offset = 0;
    if (cursorId) {
        const auto cursor = std::find_if(view.Cursors.begin(), view.Cursors.begin() + view.CursorCount,
                                        [cursorId](const auto &value) { return value.Id == cursorId; });
        if (cursor == view.Cursors.begin() + view.CursorCount || cursor->Query != query) return {Status::InvalidCursor};
        offset = cursor->Offset;
    }
    if (offset > total) throw std::logic_error("cursor offset exceeds frozen record count");
    const bool more = total - offset > query.PageSize;
    State::Cursor *next = nullptr;
    if (more) {
        // Replaying a page reuses its cursor instead of consuming more storage.
        for (uint32_t i = 0; i < view.CursorCount; ++i)
            if (view.Cursors[i].Query == query && view.Cursors[i].Offset == offset + query.PageSize) next = &view.Cursors[i];
        if (!next && view.CursorCount == Limits::MaximumCursors) return {Status::LimitExceeded};
    }
    Response response;
    response.Page = ProjectQuery(snapshot, view.Descriptor, query, offset);
    if (more) {
        if (!next) {
            next = &view.Cursors[view.CursorCount++];
            *next = {Allocate(Data->NextCursor), query, offset + query.PageSize};
        }
        response.Page->NextCursor = next->Id;
    }
    return Data->Prepare(*observer, std::move(response));
}

PreparedResponse ViewService::ReadFacts(ObserverId id, ViewId viewId, uint64_t after, uint32_t limit) {
    auto *observer = Data->Find(id);
    if (!observer) return {Status::PermissionDenied};
    if (const auto status = Data->Check(*observer, viewId); status != Status::Ok) return {status};
    if (!observer->Grant.Facts) return {Status::PermissionDenied};
    if (observer->Pending || limit > Limits::MaximumFacts) return {Status::LimitExceeded};
    if (!limit) return {Status::InvalidArgument};
    const auto &snapshot = observer->Active->Snapshot;
    auto facts = Data->Source.ReadFacts(snapshot.Stamp.WorldEpoch, after, snapshot.FactHighWatermark, limit);
    Response response;
    switch (facts.Status) {
    case api::FactReadStatus::Available: break;
    case api::FactReadStatus::Gap: response.Result = Status::Gap; break;
    case api::FactReadStatus::StaleEpoch: observer->Release(); return {Status::ViewExpired};
    case api::FactReadStatus::InvalidRange: return {Status::InvalidArgument};
    case api::FactReadStatus::LimitExceeded: return {Status::LimitExceeded};
    }
    response.FactView = observer->Active->Descriptor;
    response.Facts = std::move(facts);
    return Data->Prepare(*observer, std::move(response));
}

Response ViewService::Deliver(ObserverId id, DeliveryId delivery) {
    auto *observer = Data->Find(id);
    if (!observer) return {Status::PermissionDenied};
    if (!delivery || observer->Delivery != delivery) return {Status::InvalidArgument};
    if (const auto status = Data->Check(*observer, observer->DeliveryView); status != Status::Ok) return {status};
    if (!observer->Pending) return {Status::InvalidArgument};
    auto result = std::move(*observer->Pending);
    observer->Pending.reset();
    return result;
}

Status ViewService::CloseView(ObserverId id, ViewId view) {
    auto *observer = Data->Find(id);
    if (!observer) return Status::PermissionDenied;
    const auto status = Data->Check(*observer, view);
    if (status == Status::Ok) observer->Release();
    return status;
}

Metrics ViewService::Statistics() const {
    Metrics metrics;
    metrics.ServiceFrame = Data->Frame;
    metrics.MaximumRetainedBytes += sizeof(State);
    metrics.RetainedBytes = sizeof(State);
    for (const auto &observer : Data->Observers) {
        metrics.ActiveViews += bool(observer.Active);
        metrics.PendingDeliveries += bool(observer.Pending);
        metrics.RetainedBytes += observer.RetainedBytes() - sizeof(State::Observer);
    }
    return metrics;
}
} // namespace pixel::llm
