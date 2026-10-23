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
    case api::Topic::History: return "history";
    case api::Topic::Count: break;
    }
    throw std::logic_error("unknown observation topic");
}

Policy Policy::FullAccess() {
    Policy policy;
    policy.Topics.fill(true);
    policy.Facts = true;
    policy.AllInventories = true;
    policy.AllKnowledge = true;
    return policy;
}

uint64_t QueryPage::RetainedBytes() const {
    uint64_t bytes = sizeof(*this) + View.Stamp.WorldId.capacity() + 1
        + Cells.capacity() * sizeof(CellSample) + WindSummaries.capacity() * sizeof(api::WindSummary)
        + Projectiles.capacity() * sizeof(api::ProjectileObservation)
        + Inventories.capacity() * sizeof(api::InventoryObservation)
        + Devices.capacity() * sizeof(api::DeviceObservation)
        + Processes.capacity() * sizeof(api::ProcessObservation)
        + FieldSources.capacity() * sizeof(api::FieldSourceObservation)
        + Discoveries.capacity() * sizeof(knowledge::Discovery)
        + History.capacity() * sizeof(knowledge::HistoryRecord);
    for (const auto &inventory : Inventories) bytes += inventory.RetainedBytes() - sizeof(inventory);
    for (const auto &record : History) bytes += record.Source.CommittedAt.WorldId.capacity() + 1;
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
    if (History) {
        bytes += History->Records.capacity() * sizeof(knowledge::HistoryRecord);
        for (const auto &record : History->Records) bytes += record.Source.CommittedAt.WorldId.capacity() + 1;
    }
    if (DeliveredHistory) {
        bytes += sizeof(*DeliveredHistory) + DeliveredHistory->capacity() * sizeof(HistoryReference);
        for (const auto &source : *DeliveredHistory) bytes += source.RetainedBytes() - sizeof(source);
    }
    return bytes;
}

namespace {
void ValidatePolicy(const Policy &policy) {
    if (policy.Facts && (!policy.AllInventories || !policy.AllKnowledge))
        throw std::invalid_argument("session facts require full inventory and knowledge grants");
    if (policy.InventoryOwner && (!policy.InventoryOwner->WorldEpoch || !policy.InventoryOwner->OwnerActor))
        throw std::invalid_argument("inventory owner grant requires a nonzero epoch and actor");
    if (policy.KnowledgeOwner && (!policy.KnowledgeOwner->WorldEpoch || !policy.KnowledgeOwner->OwnerActor))
        throw std::invalid_argument("knowledge owner grant requires a nonzero epoch and actor");
    if (policy.Region && (!ValidRect(*policy.Region) || policy.Facts
        || policy.Topics[size_t(api::Topic::Programs)] || policy.Topics[size_t(api::Topic::Runtime)]))
        throw std::invalid_argument("spatial observer cannot grant session-wide summaries or facts");
    if (policy.AuthoritySubject && (!*policy.AuthoritySubject || !policy.Region
        || policy.Topics[size_t(api::Topic::History)]))
        throw std::invalid_argument("authority observer requires a subject, region and spatial topics");
}

bool HistoryAllowed(const Policy &policy) {
    return policy.Topics[size_t(api::Topic::History)] && policy.Facts
        && policy.AllInventories && policy.AllKnowledge && !policy.Region;
}

void AuthorizeKnowledge(api::ObservationSnapshot &snapshot, const Policy &policy) {
    auto &knowledge = snapshot.Knowledge;
    // Older empty sources need not manufacture a domain stamp. Actual knowledge
    // values must always belong to the same committed snapshot.
    if (knowledge.Stamp == api::CommitStamp{} && knowledge.History.empty() && knowledge.Discoveries.empty())
        return;
    if (knowledge.Stamp != snapshot.Stamp)
        throw std::logic_error("knowledge observations do not belong to the snapshot commit");
    std::vector<knowledge::Discovery> discoveries;
    if (policy.Topics[size_t(api::Topic::Knowledge)]) {
        for (const auto &entry : knowledge.Discoveries) {
            const bool owner = policy.KnowledgeOwner && policy.KnowledgeOwner->WorldEpoch == snapshot.Stamp.WorldEpoch
                && policy.KnowledgeOwner->OwnerActor == entry.OwnerActor;
            if ((policy.AllKnowledge || owner) && Contains(snapshot.Scope, {entry.Location, 1, 1})) {
                auto visible = entry;
                // Archive sequence numbers reveal how much global history came
                // before this discovery, even without returning those records.
                if (!HistoryAllowed(policy)) visible.EvidenceSequence = 0;
                discoveries.push_back(visible);
            }
        }
    }
    knowledge.Discoveries = std::move(discoveries);
    if (!HistoryAllowed(policy)) {
        std::vector<knowledge::HistoryRecord>{}.swap(knowledge.History);
        knowledge.FirstSequence = 1;
        knowledge.HighWatermark = 0;
    }
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

void AuthorizeRegion(api::ObservationSnapshot &snapshot, const Policy &policy) {
    if (!policy.AuthoritySubject) return;
    // Dynamic observers retain only spatially authorized records. This also
    // prevents service storage metrics from counting hidden global objects.
    snapshot.FactHighWatermark = 0;
    snapshot.Programs = {};
    std::vector<api::ProjectileObservation>{}.swap(snapshot.Projectiles);
    if (!policy.Topics[size_t(api::Topic::Player)] || (snapshot.Player
        && !PositionInside(snapshot.Scope, snapshot.Player->Location))) snapshot.Player.reset();
    auto &gameplay = snapshot.Gameplay;
    std::vector<api::ProcessObservation>{}.swap(gameplay.Processes);
    gameplay.Garden.reset();
    std::vector<api::DeviceObservation> devices;
    if (policy.Topics[size_t(api::Topic::Devices)]) {
        for (const auto &device : gameplay.Devices)
            if (Contains(snapshot.Scope, {device.Anchor, 1, 1})) devices.push_back(device);
    }
    gameplay.Devices = std::move(devices);
    std::vector<api::FieldSourceObservation> sources;
    if (policy.Topics[size_t(api::Topic::FieldSources)]) {
        for (const auto &source : gameplay.FieldSources)
            if (PositionInside(snapshot.Scope, source.Center)) sources.push_back(source);
    }
    gameplay.FieldSources = std::move(sources);
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
        uint64_t SourceAuthorizationVersion = 0;
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
    api::ObservationAuthorization Authorization(const Observer &observer) const {
        const auto &grant = observer.Grant;
        return grant.AuthoritySubject
            ? Source.AuthorizeObservation(*grant.AuthoritySubject, *grant.Region)
            : api::ObservationAuthorization{0, true};
    }
    void Revoke(Observer &observer) {
        if (observer.Active) observer.RevokedView = observer.Active->Descriptor.Id;
        observer.Release();
    }
    Status Check(Observer &observer, ViewId id) {
        if (id && id == observer.RevokedView) return Status::ViewRevoked;
        if (!observer.Active || observer.Active->Descriptor.Id != id) return Status::ViewExpired;
        auto &view = *observer.Active;
        if (view.AuthorizationVersion != observer.AuthorizationVersion)
            throw std::logic_error("stale authorization retained by view service");
        const auto authorization = Authorization(observer);
        if (!authorization.Allowed || authorization.Version != view.SourceAuthorizationVersion) {
            Revoke(observer);
            return Status::ViewRevoked;
        }
        if (Frame >= view.Descriptor.ExpiresAtServiceFrame
            || Source.ObservationService().LastCommitted.WorldEpoch != view.Descriptor.Stamp.WorldEpoch) {
            observer.Release();
            return Status::ViewExpired;
        }
        return Status::Ok;
    }
    PreparedResponse Prepare(Observer &observer, Response response) {
        // Retain provenance under the same byte budget as the pending response.
        // It becomes externally usable only if Deliver passes the current grant.
        if (response.Result == Status::Ok) {
            const auto *records = response.History ? &response.History->Records
                : response.Page && response.Page->Query.Topic == api::Topic::History ? &response.Page->History : nullptr;
            if (records && !records->empty()) {
                auto evidence = std::make_shared<std::vector<HistoryReference>>();
                evidence->reserve(records->size());
                for (const auto &record : *records)
                    evidence->push_back({{record.Source.CommittedAt.WorldId, record.Sequence},
                                         record.Source.Id, record.Source.CommittedAt});
                response.DeliveredHistory = std::move(evidence);
            }
        }
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
    if (observer->Grant.AuthoritySubject && policy.AuthoritySubject != observer->Grant.AuthoritySubject)
        throw std::invalid_argument("authority observer subject cannot be detached or replaced");
    if (observer->AuthorizationVersion == UINT64_MAX) throw std::overflow_error("observer authorization exhausted");
    ++observer->AuthorizationVersion;
    Data->Revoke(*observer);
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
    auto *observer = Data->Find(id);
    if (observer && observer->Grant.AuthoritySubject && observer->Active)
        Data->Check(*observer, observer->Active->Descriptor.Id);
    if (!observer || !Data->Authorization(*observer).Allowed) return {Status::PermissionDenied};
    Capabilities result;
    result.Region = observer->Grant.Region;
    result.Facts = observer->Grant.Facts;
    result.AllInventories = observer->Grant.AllInventories;
    result.InventoryOwner = observer->Grant.InventoryOwner;
    result.AllKnowledge = observer->Grant.AllKnowledge;
    result.KnowledgeOwner = observer->Grant.KnowledgeOwner;
    for (size_t i = 0; i < observer->Grant.Topics.size(); ++i)
        if (observer->Grant.Topics[i] && (api::Topic(i) != api::Topic::History || HistoryAllowed(observer->Grant)))
            result.Topics.push_back({api::Topic(i), api::TopicSupport(api::Topic(i))});
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
    const auto authorization = Data->Authorization(*observer);
    if (observer->Grant.AuthoritySubject && observer->Active)
        Data->Check(*observer, observer->Active->Descriptor.Id);
    if (!authorization.Allowed) return {Status::PermissionDenied};
    auto capture = Data->Source.CaptureObservation(scope);
    if (capture.Status != api::ObservationStatus::Available) return {CaptureStatus(capture.Status)};
    if (!capture.Snapshot || capture.Snapshot->Scope != scope
        || capture.Snapshot->Cells.size() != size_t(scope.Width) * scope.Height)
        throw std::logic_error("observation source violated bounded snapshot contract");
    AuthorizeGameplay(*capture.Snapshot, observer->Grant);
    AuthorizeKnowledge(*capture.Snapshot, observer->Grant);
    AuthorizeRegion(*capture.Snapshot, observer->Grant);
    if (capture.Snapshot->RetainedBytes() + sizeof(State::View) + sizeof(State::Observer)
        + capture.Snapshot->Stamp.WorldId.capacity() + 1 > Limits::MaximumViewBytes)
        return {Status::LimitExceeded};
    // A failed capture (especially a prepared simulation step) preserves the
    // previous view. Replace only after validating the new owned snapshot.
    observer->Release();
    auto view = std::make_unique<State::View>();
    view->Snapshot = std::move(*capture.Snapshot);
    view->AuthorizationVersion = observer->AuthorizationVersion;
    view->SourceAuthorizationVersion = authorization.Version;
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
    if (query.Topic == api::Topic::History && !HistoryAllowed(observer->Grant)) return {Status::PermissionDenied};
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

PreparedResponse ViewService::ReadHistory(ObserverId id, ViewId viewId, uint64_t after, uint32_t limit) {
    auto *observer = Data->Find(id);
    if (!observer) return {Status::PermissionDenied};
    if (const auto status = Data->Check(*observer, viewId); status != Status::Ok) return {status};
    if (!HistoryAllowed(observer->Grant)) return {Status::PermissionDenied};
    if (observer->Pending) return {Status::LimitExceeded};
    auto history = knowledge::ReadHistory(observer->Active->Snapshot.Knowledge, after, limit);
    Response response;
    switch (history.Status) {
    case knowledge::ReadStatus::Available: break;
    case knowledge::ReadStatus::Gap: response.Result = Status::Gap; break;
    case knowledge::ReadStatus::InvalidRange: return {Status::InvalidArgument};
    case knowledge::ReadStatus::LimitExceeded: return {Status::LimitExceeded};
    }
    response.FactView = observer->Active->Descriptor;
    response.History = std::move(history);
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
