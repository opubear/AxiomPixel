#include "Runtime/Gameplay/LLMView/QueryProjection.h"
#include "Runtime/Gameplay/LLMView/ViewService.h"
#include <iostream>
#include <stdexcept>

using namespace pixel;
namespace {
constexpr uint64_t Subject = 41;
constexpr api::Rect Region{{0, 0}, 16, 16};
constexpr api::Rect Scope{{2, 2}, 4, 1};

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
void InvalidPolicy(Function operation) {
    try { operation(); }
    catch (const std::invalid_argument &) { return; }
    throw std::runtime_error("invalid host policy was accepted");
}

class Source : public api::ObservationSource {
  public:
    api::ObservationSnapshot Value;
    uint32_t Captures = 0;
    mutable uint32_t FactReads = 0;

    Source() {
        Value.Stamp = {"authority-view", 7, 4, 9};
        Value.Gameplay.Stamp = Value.Knowledge.Stamp = Value.Stamp;
        Value.FactHighWatermark = 200;
        Value.Programs.RunningRoots = 30;
    }
    api::ObservationResult CaptureObservation(api::Rect scope) override {
        ++Captures;
        auto result = Value;
        result.Scope = scope;
        result.Cells.resize(size_t(scope.Width) * scope.Height);
        for (auto &cell : result.Cells) {
            cell.MaterialId = 1;
            cell.MaterialSampledAtRevision = Value.Stamp.Revision;
        }
        result.ResidentCellCount = uint32_t(result.Cells.size());
        return {api::ObservationStatus::Available, std::move(result)};
    }
    api::ObservationServiceStatus ObservationService() const override {
        return {Value.Stamp, true, false};
    }
    api::FactReadResult ReadFacts(uint64_t, uint64_t, uint64_t, uint32_t) const override {
        ++FactReads;
        return {};
    }
};

class AuthoritySource final : public Source {
  public:
    api::ObservationAuthorization Grant{1, true};
    mutable uint32_t AuthorizationChecks = 0;
    api::ObservationAuthorization AuthorizeObservation(uint64_t subject, api::Rect region) const override {
        ++AuthorizationChecks;
        return {Grant.Version, Grant.Allowed && subject == Subject && region == Region};
    }
};

llm::Policy BoundPolicy() {
    llm::Policy policy;
    policy.AuthoritySubject = Subject;
    policy.Region = Region;
    policy.Topics[size_t(api::Topic::Materials)] = true;
    return policy;
}

llm::QuerySpec Materials() { return {api::Topic::Materials, Scope, 1, 1}; }

llm::ViewDescriptor Open(llm::ViewService &service, llm::ObserverId observer) {
    auto opened = service.OpenView(observer, Scope);
    Require(opened.Result == llm::Status::Ok && opened.View, "authorized view did not open");
    Require(!opened.View->FactHighWatermark, "region view exposed global fact watermark");
    return *opened.View;
}

llm::Response Deliver(llm::ViewService &service, llm::ObserverId observer, llm::PreparedResponse pending) {
    Require(pending.Result == llm::Status::Ok && pending.Delivery, "query did not prepare a response");
    return service.Deliver(observer, pending.Delivery);
}

void DefaultDenialAndHostBinding() {
    Source source;
    llm::ViewService service(source);
    const auto bound = service.RegisterObserver(BoundPolicy());
    Require(service.OpenView(bound, Scope).Result == llm::Status::PermissionDenied && !source.Captures,
            "a source without authority implementation admitted a bound observer");
    const auto denied = service.DescribeCapabilities(bound);
    Require(denied.Result == llm::Status::PermissionDenied && denied.Topics.empty() && !denied.Region,
            "denied observer retained a capability catalogue");

    const auto native = service.RegisterObserver(llm::Policy::FullAccess());
    Require(service.OpenView(native, Scope).Result == llm::Status::Ok && source.Captures == 1,
            "default authority denial changed static policy behavior");
    auto invalid = BoundPolicy();
    invalid.Region.reset();
    InvalidPolicy([&] { service.RegisterObserver(invalid); });
    invalid = BoundPolicy();
    invalid.AuthoritySubject = 0;
    InvalidPolicy([&] { service.RegisterObserver(invalid); });
    for (const auto topic : {api::Topic::Programs, api::Topic::Runtime, api::Topic::History}) {
        invalid = BoundPolicy();
        invalid.Topics[size_t(topic)] = true;
        InvalidPolicy([&] { service.RegisterObserver(invalid); });
    }
    invalid = BoundPolicy();
    invalid.Facts = invalid.AllInventories = invalid.AllKnowledge = true;
    InvalidPolicy([&] { service.RegisterObserver(invalid); });
    InvalidPolicy([&] { service.SetPolicy(bound, llm::Policy::FullAccess()); });
    invalid = BoundPolicy();
    invalid.AuthoritySubject = Subject + 1;
    InvalidPolicy([&] { service.SetPolicy(bound, invalid); });
    service.SetPolicy(bound, BoundPolicy());
    Require(service.OpenView(bound, Scope).Result == llm::Status::PermissionDenied,
            "SetPolicy bypassed the source's current authority denial");
}

void VersionChangesRevokeViewsCursorsAndDelivery() {
    AuthoritySource source;
    llm::ViewService service(source);
    const auto observer = service.RegisterObserver(BoundPolicy());
    const auto emptyBytes = service.Statistics().RetainedBytes;
    const auto firstView = Open(service, observer);
    auto first = Deliver(service, observer, service.Query(observer, firstView.Id, Materials()));
    Require(first.Result == llm::Status::Ok && first.Page && first.Page->NextCursor,
            "first authorized page did not create a cursor");
    const auto cursor = first.Page->NextCursor;
    const auto pending = service.Query(observer, firstView.Id, Materials(), cursor);
    Require(pending.Result == llm::Status::Ok && service.Statistics().PendingDeliveries == 1,
            "second page was not retained for delivery");
    ++source.Grant.Version; // Still allowed: a changed grant requires a new frozen view.
    Require(service.Query(observer, firstView.Id, Materials(), cursor).Result == llm::Status::ViewRevoked,
            "old cursor survived an authority version change");
    const auto stale = service.Deliver(observer, pending.Delivery);
    Require(stale.Result == llm::Status::ViewRevoked && !stale.Page && !stale.Facts && !stale.History,
            "revoked prepared data escaped through Deliver");
    Require(service.ReadFacts(observer, firstView.Id, 0).Result == llm::Status::ViewRevoked
            && service.ReadHistory(observer, firstView.Id, 0).Result == llm::Status::ViewRevoked,
            "fact or history entry point ignored revoked view identity");
    const auto cleared = service.Statistics();
    Require(!cleared.ActiveViews && !cleared.PendingDeliveries && cleared.RetainedBytes == emptyBytes,
            "revocation retained snapshot, cursor or delivery storage");

    const auto fresh = Open(service, observer);
    Require(fresh.Id != firstView.Id
            && service.Query(observer, fresh.Id, Materials(), cursor).Result == llm::Status::InvalidCursor,
            "a new view accepted the previous view's cursor");
    Require(Deliver(service, observer, service.Query(observer, fresh.Id, Materials())).Result == llm::Status::Ok,
            "new view did not adopt current allowed authority");
    auto delivery = service.Query(observer, fresh.Id, Materials());
    source.Grant.Allowed = false; // Denial must revoke even without a version bump.
    Require(service.Deliver(observer, delivery.Delivery).Result == llm::Status::ViewRevoked,
            "Deliver did not recheck current region authority");
    const auto captures = source.Captures;
    Require(service.OpenView(observer, Scope).Result == llm::Status::PermissionDenied
            && source.Captures == captures, "denied open captured hidden world state");
    const auto denied = service.DescribeCapabilities(observer);
    Require(denied.Result == llm::Status::PermissionDenied && denied.Topics.empty(),
            "DescribeCapabilities advertised revoked topics");

    source.Grant = {source.Grant.Version + 1, true};
    const auto restored = Open(service, observer);
    Require(Deliver(service, observer, service.Query(observer, restored.Id, Materials())).Result == llm::Status::Ok,
            "restored region authority could not open and query a fresh view");
    auto wrongScope = Scope;
    wrongScope.Origin.X = 40;
    Require(service.OpenView(observer, wrongScope).Result == llm::Status::PermissionDenied,
            "authority binding bypassed the static region restriction");
    Require(source.AuthorizationChecks > source.Captures,
            "authority was only checked when capturing a snapshot");
}

void EveryEntryPointChecksCurrentAuthority() {
    AuthoritySource source;
    llm::ViewService service(source);
    const auto observer = service.RegisterObserver(BoundPolicy());
    auto view = Open(service, observer);
    ++source.Grant.Version;
    Require(service.ReadFacts(observer, view.Id, 0).Result == llm::Status::ViewRevoked && !source.FactReads,
            "ReadFacts did not revoke before consulting the fact source");
    view = Open(service, observer);
    ++source.Grant.Version;
    Require(service.ReadHistory(observer, view.Id, 0).Result == llm::Status::ViewRevoked,
            "ReadHistory did not recheck current authority");
    view = Open(service, observer);
    auto pending = service.Query(observer, view.Id, Materials());
    ++source.Grant.Version;
    service.AdvanceServiceFrame();
    Require(!service.Statistics().ActiveViews && !service.Statistics().PendingDeliveries
            && service.Deliver(observer, pending.Delivery).Result == llm::Status::ViewRevoked,
            "idle service frame did not discard revoked storage");

    view = Open(service, observer);
    ++source.Grant.Version;
    Require(service.DescribeCapabilities(observer).Result == llm::Status::Ok
            && !service.Statistics().ActiveViews
            && service.Query(observer, view.Id, Materials()).Result == llm::Status::ViewRevoked,
            "capability refresh failed to revoke the previous authority version");
    view = Open(service, observer);
    const auto pendingBeforePolicy = service.Query(observer, view.Id, Materials());
    auto revised = BoundPolicy();
    revised.Topics[size_t(api::Topic::Devices)] = true;
    service.SetPolicy(observer, revised);
    Require(service.Deliver(observer, pendingBeforePolicy.Delivery).Result == llm::Status::ViewRevoked,
            "host policy change retained prepared data under the old grant");

    const auto native = service.RegisterObserver(llm::Policy::FullAccess());
    const auto nativeView = service.OpenView(native, Scope);
    Require(nativeView.Result == llm::Status::Ok && nativeView.View, "static view did not open");
    source.Grant.Allowed = false;
    ++source.Grant.Version;
    service.AdvanceServiceFrame();
    Require(Deliver(service, native, service.Query(native, nativeView.View->Id, Materials())).Result == llm::Status::Ok,
            "dynamic region revocation invalidated an unrelated static observer");
}

void RegionFilteringKeepsPrivateRecordsBounded() {
    AuthoritySource source;
    llm::ViewService service(source);
    auto policy = BoundPolicy();
    for (const auto topic : {api::Topic::Items, api::Topic::Devices, api::Topic::Knowledge})
        policy.Topics[size_t(topic)] = true;
    policy.InventoryOwner = llm::InventoryGrant{7, 9};
    policy.KnowledgeOwner = llm::KnowledgeGrant{7, 9};
    api::InventoryObservation inventory;
    inventory.OwnerActor = 9;
    inventory.Location = {{2, 2}, {0, 0}};
    inventory.ContainerId = 10;
    inventory.Quantity = 5;
    inventory.Stacks.push_back({1, 1, 5, 0});
    source.Value.Gameplay.Inventories.push_back(inventory);
    api::DeviceObservation device;
    device.Anchor = {2, 2};
    device.OwnerActor = 9;
    device.Fuel = 5;
    source.Value.Gameplay.Devices.push_back(device);
    source.Value.Knowledge.Discoveries.push_back({9, {2, 2}, 1});
    const auto observer = service.RegisterObserver(policy);
    auto view = Open(service, observer);
    const auto localBytes = service.Statistics().RetainedBytes;
    service.CloseView(observer, view.Id);

    // Same-owner records outside the region and another owner's private data
    // inside it must not change visible counts or retained storage.
    inventory.Location.AnchorCell = {40, 40};
    source.Value.Gameplay.Inventories.push_back(inventory);
    inventory.Location.AnchorCell = {2, 2};
    inventory.OwnerActor = 100;
    source.Value.Gameplay.Inventories.push_back(inventory);
    device.Anchor = {40, 40};
    source.Value.Gameplay.Devices.push_back(device);
    source.Value.Gameplay.Processes.resize(70);
    source.Value.Projectiles.resize(70);
    source.Value.Knowledge.Discoveries.push_back({9, {40, 40}, 9});
    source.Value.Knowledge.Discoveries.push_back({100, {2, 2}, 10});
    view = Open(service, observer);
    Require(service.Statistics().RetainedBytes == localBytes,
            "hidden global or private records changed retained-memory statistics");
    auto inventories = Deliver(service, observer, service.Query(observer, view.Id, {api::Topic::Items, Scope}));
    Require(inventories.Page && inventories.Page->TotalRecords == 1
            && inventories.Page->Inventories.size() == 1 && inventories.Page->Inventories[0].Quantity == 5,
            "region observation leaked cross-region or private inventory counts");
    auto devices = Deliver(service, observer, service.Query(observer, view.Id, {api::Topic::Devices, Scope}));
    Require(devices.Page && devices.Page->TotalRecords == 1 && devices.Page->Devices[0].Fuel == 5,
            "region device projection leaked another region or discarded explicit owner access");
    auto knowledge = Deliver(service, observer, service.Query(observer, view.Id, {api::Topic::Knowledge, Scope}));
    Require(knowledge.Page && knowledge.Page->TotalRecords == 1
            && knowledge.Page->Discoveries[0].EvidenceSequence == 0,
            "region knowledge exposed hidden owners or global history sequence");
    Require(service.Query(observer, view.Id, {api::Topic::Programs, Scope}).Result == llm::Status::PermissionDenied
            && service.Query(observer, view.Id, {api::Topic::Runtime, Scope}).Result == llm::Status::PermissionDenied
            && service.Query(observer, view.Id, {api::Topic::History, Scope}).Result == llm::Status::PermissionDenied
            && service.ReadFacts(observer, view.Id, 0).Result == llm::Status::PermissionDenied
            && service.ReadHistory(observer, view.Id, 0).Result == llm::Status::PermissionDenied,
            "regional authority granted global summaries or facts");

    policy.InventoryOwner.reset();
    service.SetPolicy(observer, policy);
    view = Open(service, observer);
    devices = Deliver(service, observer, service.Query(observer, view.Id, {api::Topic::Devices, Scope}));
    Require(devices.Page && !devices.Page->Devices[0].Fuel,
            "region authority implicitly granted private device fuel");
    inventories = Deliver(service, observer, service.Query(observer, view.Id, {api::Topic::Items, Scope}));
    Require(inventories.Page && inventories.Page->TotalRecords == 0 && inventories.Page->Inventories.empty(),
            "region authority implicitly granted inventory access");
}
} // namespace

int main() {
    try {
        DefaultDenialAndHostBinding();
        VersionChangesRevokeViewsCursorsAndDelivery();
        EveryEntryPointChecksCurrentAuthority();
        RegionFilteringKeepsPrivateRecordsBounded();
        std::cout << "Authority view contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
