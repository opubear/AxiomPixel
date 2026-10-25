#pragma once
#include "Runtime/Gameplay/GameAPI/ObservationSource.h"
#include "DerivedMemory.h"
#include <memory>

namespace pixel::llm {
using ObserverId = uint64_t;
using ViewId = uint64_t;
using CursorId = uint64_t;
using DeliveryId = uint64_t;

enum class Status {
    Ok, PermissionDenied, ViewRevoked, ViewExpired, InvalidArgument,
    InvalidCursor, LimitExceeded, Unsupported, NotSampled, SessionUnavailable, Gap
};
const char *StatusName(Status status);
const char *TopicName(api::Topic topic);

struct Limits {
    static constexpr uint32_t MaximumObservers = 3, MaximumEdge = 64;
    static constexpr uint32_t MaximumPageCells = 256, MaximumPages = 16;
    static constexpr uint32_t MaximumCursors = 64, MaximumFacts = 256;
    static constexpr uint64_t MaximumViewBytes = 1024 * 1024, LeaseFrames = 120;
};
// Host-owned configuration, never decoded from an external request. A bounded
// region cannot grant session-wide Programs/Runtime/Facts; those have no spatial
// projection contract yet. Topic denial also hides its capability catalogue.
struct InventoryGrant {
    uint64_t WorldEpoch = 0, OwnerActor = 0;
};
struct KnowledgeGrant {
    uint64_t WorldEpoch = 0, OwnerActor = 0;
};
struct Policy {
    std::array<bool, size_t(api::Topic::Count)> Topics{};
    std::optional<api::Rect> Region;
    // Dynamic region authority, bound by the host when registering an observer.
    // Never decoded from JSON; SetPolicy cannot detach or replace this subject.
    std::optional<uint64_t> AuthoritySubject;
    bool Facts = false; // Requires AllInventories + AllKnowledge for private facts.
    bool AllInventories = false;
    std::optional<InventoryGrant> InventoryOwner;
    bool AllKnowledge = false;
    std::optional<KnowledgeGrant> KnowledgeOwner;
    static Policy FullAccess();
};
struct Capability {
    api::Topic Topic;
    api::Availability Support;
};
struct Capabilities {
    Status Result = Status::Ok;
    uint32_t SchemaVersion = 1;
    std::vector<Capability> Topics;
    std::optional<api::Rect> Region;
    bool Facts = false;
    bool AllInventories = false;
    std::optional<InventoryGrant> InventoryOwner;
    bool AllKnowledge = false;
    std::optional<KnowledgeGrant> KnowledgeOwner;
};
struct ViewDescriptor {
    ViewId Id = 0;
    uint32_t SchemaVersion = 1;
    api::CommitStamp Stamp;
    api::Rect Scope;
    uint64_t ExpiresAtServiceFrame = 0;
    std::optional<uint64_t> FactHighWatermark;
};
struct OpenResult {
    Status Result = Status::Ok;
    std::optional<ViewDescriptor> View;
};
struct QuerySpec {
    api::Topic Topic = api::Topic::Materials;
    api::Rect Scope;
    uint32_t ResolutionCells = 1, PageSize = Limits::MaximumPageCells, SchemaVersion = 1;
    bool operator==(const QuerySpec &) const = default;
};
struct CellSample {
    api::Cell Location;
    std::optional<uint32_t> MaterialId; // Only Materials returns this field.
    api::AirSampleKind AirKind = api::AirSampleKind::NotResident;
    std::optional<std::array<float, 2>> AirVelocity;
    uint64_t SampledAtRevision = 0;
};
struct QueryPage {
    ViewDescriptor View;
    QuerySpec Query;
    std::string_view Unit;
    api::Availability Availability = api::Availability::Available;
    std::string_view Reason = "available";
    uint32_t RequestedCells = 0, ResidentCells = 0;
    uint64_t OldestSampledAtRevision = 0, NewestSampledAtRevision = 0;
    CursorId NextCursor = 0;
    std::vector<CellSample> Cells;
    std::optional<api::WindSampling> Wind;
    std::vector<api::WindSummary> WindSummaries;
    std::optional<api::PlayerObservation> Player;
    std::optional<api::ProgramObservation> Programs;
    std::optional<bool> Paused;
    std::vector<api::ProjectileObservation> Projectiles;
    // Counts include only authorized records inside Query.Scope, before paging.
    uint32_t TotalRecords = 0;
    std::vector<api::InventoryObservation> Inventories; // One entire container per record.
    std::vector<api::DeviceObservation> Devices;
    std::vector<api::ProcessObservation> Processes;
    std::vector<api::FieldSourceObservation> FieldSources;
    std::vector<knowledge::Discovery> Discoveries;
    std::vector<knowledge::HistoryRecord> History;
    // History is a global archive; Query.Scope does not spatially filter it.
    // Metadata exists only on an authorized History page.
    std::optional<uint64_t> HistoryFirstSequence, HistoryHighWatermark;
    uint64_t RetainedBytes() const;
};
struct Response {
    Response(Status status = Status::Ok) : Result(status) {}
    Status Result = Status::Ok;
    std::optional<QueryPage> Page;
    std::optional<ViewDescriptor> FactView;
    std::optional<api::FactReadResult> Facts;
    std::optional<knowledge::HistoryRead> History;
    uint64_t RetainedBytes() const;

  private:
    // The service issues this immutable proof only through Deliver. Copying or
    // editing a public response cannot introduce a never-delivered citation.
    std::shared_ptr<const std::vector<HistoryReference>> DeliveredHistory;
    friend class ViewService;
    friend class DerivedMemoryStore;
};
struct PreparedResponse {
    Status Result = Status::Ok;
    DeliveryId Delivery = 0;
};
struct Metrics {
    uint64_t ServiceFrame = 0, RetainedBytes = 0;
    uint64_t MaximumRetainedBytes = Limits::MaximumObservers * Limits::MaximumViewBytes;
    uint32_t ActiveViews = 0, PendingDeliveries = 0;
};

// Single host thread owns this service and its source. Query/ReadFacts prepare a
// bounded, owned value; Deliver is the authorization boundary before handing it
// to JSON, a worker or another consumer. No mutable domain pointer escapes.
class ViewService {
  public:
    explicit ViewService(api::ObservationSource &source);
    ~ViewService();
    ViewService(const ViewService &) = delete;
    ViewService &operator=(const ViewService &) = delete;

    ObserverId RegisterObserver(Policy policy);
    void SetPolicy(ObserverId observer, Policy policy);
    void RemoveObserver(ObserverId observer);
    void AdvanceServiceFrame(); // Exactly once per host service iteration, even paused/idle.
    Capabilities DescribeCapabilities(ObserverId observer) const;
    OpenResult OpenView(ObserverId observer, api::Rect scope);
    PreparedResponse Query(ObserverId observer, ViewId view, QuerySpec query, CursorId cursor = 0);
    PreparedResponse ReadFacts(ObserverId observer, ViewId view, uint64_t after, uint32_t limit = 256);
    PreparedResponse ReadHistory(ObserverId observer, ViewId view, uint64_t after, uint32_t limit = 128);
    Response Deliver(ObserverId observer, DeliveryId delivery);
    Status CloseView(ObserverId observer, ViewId view);
    Metrics Statistics() const;

  private:
    struct State;
    std::unique_ptr<State> Data;
};
} // namespace pixel::llm
