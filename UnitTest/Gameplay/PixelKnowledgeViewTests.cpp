#include "LLMView/DerivedMemory.h"
#include "LLMView/JsonAdapter.h"
#include "LLMView/QueryProjection.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace pixel;
namespace {
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
constexpr api::Rect Scope{{0, 0}, 8, 8};
constexpr uint64_t Huge = (uint64_t{1} << 54) + 7;

class Source final : public api::ObservationSource {
  public:
    api::ObservationSnapshot Value;
    Source() {
        Value.Stamp = {"knowledge-world", 7, 10, 20};
        Value.Scope = Scope;
        Value.Gameplay.Stamp = Value.Stamp;
        Value.Knowledge.Stamp = Value.Stamp;
        Value.Knowledge.Discoveries = {
            {11, {1, 1}, 1, true, false, false, false},
            {22, {2, 1}, 2, true, true, false, false},
            {11, {3, 1}, 3, true, false, true, false},
            {11, {7, 7}, 4, true, false, false, true}
        };
        History(1, 4);
    }
    void History(uint64_t first, uint32_t count) {
        auto &knowledge = Value.Knowledge;
        knowledge.History.clear();
        knowledge.FirstSequence = first;
        knowledge.HighWatermark = first + count - 1;
        for (uint64_t i = first; i <= knowledge.HighWatermark; ++i) {
            api::DomainFact fact;
            fact.Id = {Value.Stamp.WorldEpoch, i + 100};
            fact.CommittedAt = Value.Stamp;
            fact.Cause = api::RequestKey{Value.Stamp.WorldEpoch, 42, i};
            fact.Payload = api::ForceObserved{11, {1, 1}, {3600, 0}};
            knowledge.History.push_back({i, std::move(fact)});
        }
    }
    void Commit() {
        ++Value.Stamp.Revision;
        Value.Gameplay.Stamp = Value.Stamp;
        Value.Knowledge.Stamp = Value.Stamp;
    }
    api::ObservationResult CaptureObservation(api::Rect scope) override {
        auto result = Value;
        result.Scope = scope;
        result.Cells.resize(size_t(scope.Width) * scope.Height);
        return {api::ObservationStatus::Available, std::move(result)};
    }
    api::ObservationServiceStatus ObservationService() const override { return {Value.Stamp, true, false}; }
    api::FactReadResult ReadFacts(uint64_t epoch, uint64_t after, uint64_t high, uint32_t) const override {
        api::FactReadResult result;
        result.WorldEpoch = epoch;
        result.HighWatermark = high;
        result.NextSequence = after;
        return result;
    }
};

llm::Response Deliver(llm::ViewService &service, llm::ObserverId observer, llm::PreparedResponse prepared) {
    Require(prepared.Result == llm::Status::Ok && prepared.Delivery, "response could not be prepared");
    return service.Deliver(observer, prepared.Delivery);
}
llm::QuerySpec Query(api::Topic topic, uint32_t size = 1) { return {topic, Scope, 1, size}; }
llm::Policy TopicsOnly() {
    llm::Policy policy;
    policy.Topics[size_t(api::Topic::Knowledge)] = true;
    policy.Topics[size_t(api::Topic::History)] = true;
    return policy;
}

void KnowledgeAuthorizationAndRetention() {
    Source source;
    llm::ViewService service(source);
    auto policy = TopicsOnly();
    const auto observer = service.RegisterObserver(policy);
    auto opened = service.OpenView(observer, Scope);
    auto denied = Deliver(service, observer, service.Query(observer, opened.View->Id, Query(api::Topic::Knowledge)));
    Require(denied.Page->TotalRecords == 0 && denied.Page->Discoveries.empty() && !denied.Page->NextCursor,
            "topic access exposed private discovery count or values");
    Require(service.Query(observer, opened.View->Id, Query(api::Topic::History)).Result == llm::Status::PermissionDenied
            && service.ReadHistory(observer, opened.View->Id, 0).Result == llm::Status::PermissionDenied,
            "ordinary topic access exposed global history");
    auto capabilities = llm::ToJson(service.DescribeCapabilities(observer));
    Require(capabilities["topics"].size() == 1 && capabilities["topics"][0]["knowledgeAccess"]["kind"] == "none",
            "capabilities advertised unauthorized history or knowledge grant");
    const auto retained = service.Statistics().RetainedBytes;
    source.Value.Knowledge.History.resize(10000);
    source.Value.Knowledge.Discoveries.resize(10000);
    Require(service.OpenView(observer, Scope).View.has_value() && service.Statistics().RetainedBytes == retained,
            "hidden knowledge allocations influenced retained storage");
    source = Source{};

    policy.KnowledgeOwner = llm::KnowledgeGrant{7, 11};
    policy.Region = api::Rect{{0, 0}, 4, 4};
    service.SetPolicy(observer, policy);
    Require(service.OpenView(observer, Scope).Result == llm::Status::PermissionDenied,
            "region policy permitted a larger capture");
    opened = service.OpenView(observer, *policy.Region);
    auto query = Query(api::Topic::Knowledge);
    query.Scope = *policy.Region;
    auto first = Deliver(service, observer, service.Query(observer, opened.View->Id, query));
    Require(first.Page->TotalRecords == 2 && first.Page->Discoveries[0].Location == api::Cell{1, 1}
            && first.Page->NextCursor, "owner or regional knowledge filtering ran after counts");
    const auto privateJson = llm::ToJson(first)["page"];
    Require(first.Page->Discoveries[0].EvidenceSequence == 0 && !first.Page->HistoryHighWatermark
            && privateJson["discoveries"][0]["evidence"].is_null()
            && privateJson["discoveries"][0]["evidenceAvailability"] == "permission_denied"
            && !privateJson.contains("highWatermark"),
            "owner knowledge leaked the size of global history through its citation");
    auto second = Deliver(service, observer, service.Query(observer, opened.View->Id, query, first.Page->NextCursor));
    Require(second.Page->Discoveries[0].Location == api::Cell{3, 1} && !second.Page->NextCursor
            && second.Page->Discoveries[0].CanCopy && !second.Page->Discoveries[0].Analyzed,
            "knowledge levels were conflated or owner page lost a discovery");
    const auto pending = service.Query(observer, opened.View->Id, query, first.Page->NextCursor);
    policy.KnowledgeOwner.reset();
    service.SetPolicy(observer, policy);
    Require(service.Deliver(observer, pending.Delivery).Result == llm::Status::ViewRevoked
            && service.Query(observer, opened.View->Id, query, first.Page->NextCursor).Result == llm::Status::ViewRevoked,
            "revoked knowledge escaped through a pending delivery or cursor");

    policy.KnowledgeOwner = llm::KnowledgeGrant{6, 11};
    service.SetPolicy(observer, policy);
    opened = service.OpenView(observer, *policy.Region);
    auto stale = Deliver(service, observer, service.Query(observer, opened.View->Id, query));
    Require(stale.Page->Discoveries.empty() && !stale.Page->TotalRecords,
            "actor ID reused in another epoch inherited knowledge");
    bool rejected = false;
    try { auto invalid = llm::Policy::FullAccess(); invalid.AllKnowledge = false; service.SetPolicy(observer, invalid); }
    catch (const std::invalid_argument &) { rejected = true; }
    Require(rejected, "fact authorization ignored private knowledge");
}

void FrozenHistoryAndGaps() {
    Source source;
    llm::ViewService service(source);
    auto policy = llm::Policy::FullAccess();
    const auto observer = service.RegisterObserver(policy);
    auto view = service.OpenView(observer, Scope);
    auto page = Deliver(service, observer, service.Query(observer, view.View->Id, Query(api::Topic::History, 2)));
    Require(page.Page->TotalRecords == 4 && page.Page->History.size() == 2 && page.Page->NextCursor,
            "history query did not paginate frozen records");
    auto knowledge = Deliver(service, observer, service.Query(observer, view.View->Id, Query(api::Topic::Knowledge, 4)));
    source.Value.Knowledge.Discoveries.clear();
    source.Commit();
    source.History(9, knowledge::HistoryCapacity);
    auto old = Deliver(service, observer, service.ReadHistory(observer, view.View->Id, 0, 2));
    Require(old.Result == llm::Status::Ok && old.History->HighWatermark == 4 && old.History->Records[0].Sequence == 1
            && old.History->NextSequence == 2 && old.History->HasMore,
            "frozen history mixed later commits or suffered the live archive's retention gap");
    auto next = Deliver(service, observer, service.Query(observer, view.View->Id, Query(api::Topic::History, 2), page.Page->NextCursor));
    Require(next.Page->History[0].Sequence == 3 && next.Page->View.Stamp.Revision == 20 && !next.Page->NextCursor,
            "history cursor lost frozen record identity");
    auto oldKnowledge = Deliver(service, observer, service.Query(observer, view.View->Id, Query(api::Topic::Knowledge, 4)));
    Require(oldKnowledge.Page->Discoveries.size() == knowledge.Page->Discoveries.size(), "old discoveries changed after commit");

    view = service.OpenView(observer, Scope);
    auto gap = Deliver(service, observer, service.ReadHistory(observer, view.View->Id, 0));
    Require(gap.Result == llm::Status::Gap && gap.History->FirstSequence == 9 && gap.History->Records.empty(),
            "history retention overflow hid a missing prefix");
    auto retained = Deliver(service, observer, service.ReadHistory(observer, view.View->Id, 8, 128));
    Require(retained.History->Records.size() == 128 && !retained.History->HasMore,
            "history restart at supplied firstSequence omitted retained records");
    auto metadata = Deliver(service, observer, service.Query(observer, view.View->Id, Query(api::Topic::History, 128)));
    Require(metadata.Page->Availability == api::Availability::Partial && metadata.Page->Reason == "history_retention_gap"
            && *metadata.Page->HistoryFirstSequence == 9, "history snapshot concealed retention metadata");
    Require(service.ReadHistory(observer, view.View->Id, UINT64_MAX).Result == llm::Status::InvalidArgument
            && service.ReadHistory(observer, view.View->Id, 8, 129).Result == llm::Status::LimitExceeded,
            "invalid history bounds were accepted");
    auto pending = service.ReadHistory(observer, view.View->Id, 8, 1);
    service.SetPolicy(observer, TopicsOnly());
    Require(service.Deliver(observer, pending.Delivery).Result == llm::Status::ViewRevoked,
            "revoked history reached delivery");
    source.Value.Knowledge.Stamp.Revision--;
    bool rejected = false;
    try { service.OpenView(observer, Scope); } catch (const std::logic_error &) { rejected = true; }
    Require(rejected, "mixed-commit knowledge snapshot was accepted");
}

void DerivedMemoryNeedsDeliveredEvidence() {
    Source source;
    llm::ViewService service(source);
    const auto observer = service.RegisterObserver(llm::Policy::FullAccess());
    const auto view = service.OpenView(observer, Scope);
    const auto response = Deliver(service, observer, service.ReadHistory(observer, view.View->Id, 0, 2));
    const std::array<llm::HistoryId, 1> citations{{{"knowledge-world", 1}}};
    llm::DerivedMemoryStore store;
    Require(store.Remember(response, "Observed force suggests a useful planting location.", {}).Status == llm::MemoryStatus::MissingSources,
            "unsourced model claim was accepted");
    llm::Response forged;
    forged.History = response.History;
    Require(store.Remember(forged, "Forged evidence", citations).Status == llm::MemoryStatus::InvalidSource,
            "caller-created response forged a successful delivery");
    const std::array<llm::HistoryId, 1> unseen{{{"knowledge-world", 3}}};
    Require(store.Remember(response, "Out of page", unseen).Status == llm::MemoryStatus::InvalidSource,
            "undelivered history record was accepted as evidence");
    Require(store.Remember(response, std::string(1025, 'x'), citations).Status == llm::MemoryStatus::LimitExceeded,
            "unbounded model text entered memory");
    auto changed = response;
    changed.History->Records[0].Source.CommittedAt.Revision = 999;
    auto saved = store.Remember(changed, "A model interpretation, not a committed world fact.", citations);
    Require(saved.Status == llm::MemoryStatus::Stored && store.Entries()[0].Sources[0].SourceVersion.Revision == 20
            && store.Entries()[0].Sources[0].SourceFact.Sequence == 101,
            "summary source lost the original delivered fact version");
    Require(source.Value.Knowledge.History.size() == 4 && source.Value.Knowledge.HighWatermark == 4,
            "derived text changed the authoritative source");
    auto json = llm::ToJson(store.Entries()[0]);
    Require(json["provenance"] == "model_derived" && json["sources"][0]["history"]["sequence"] == "1",
            "derived memory serialization lost explicit provenance");
    for (size_t i = 1; i < llm::DerivedMemoryStore::MaximumEntries; ++i)
        Require(store.Remember(response, "Bounded", citations).Status == llm::MemoryStatus::Stored,
                "bounded store rejected an available slot");
    Require(store.Remember(response, "Overflow", citations).Status == llm::MemoryStatus::LimitExceeded
            && store.RetainedBytes() <= llm::DerivedMemoryStore::MaximumRetainedBytes,
            "model memory exceeded its entry or byte budget");

    auto pending = service.ReadHistory(observer, view.View->Id, 0, 1);
    service.SetPolicy(observer, TopicsOnly());
    auto revoked = service.Deliver(observer, pending.Delivery);
    llm::DerivedMemoryStore afterRevocation;
    Require(afterRevocation.Remember(revoked, "Not delivered", citations).Status == llm::MemoryStatus::InvalidSource,
            "revoked prepared response minted memory provenance");
}

void ExactJsonIdentifiers() {
    Source source;
    source.Value.Stamp.WorldEpoch = Huge;
    source.Value.Stamp.Revision = Huge + 1;
    source.Value.Gameplay.Stamp = source.Value.Knowledge.Stamp = source.Value.Stamp;
    source.History(Huge, 2);
    source.Value.Knowledge.Discoveries[0].OwnerActor = Huge;
    source.Value.Knowledge.Discoveries[0].EvidenceSequence = Huge;
    source.Value.Knowledge.Discoveries[0].Location = {INT64_MAX - 40, INT64_MIN + 40};
    llm::ViewService service(source);
    const auto observer = service.RegisterObserver(llm::Policy::FullAccess());
    const auto view = service.OpenView(observer, Scope);
    llm::JsonAdapter adapter(service);
    nlohmann::json request{{"method", "ReadHistory"}, {"viewId", std::to_string(view.View->Id)},
                           {"after", std::to_string(Huge - 1)}, {"limit", 1}};
    const auto json = nlohmann::json::parse(adapter.Handle(observer, request.dump()));
    Require(json["history"]["firstSequence"] == std::to_string(Huge)
            && json["history"]["records"][0]["id"]["sequence"] == std::to_string(Huge)
            && json["history"]["records"][0]["sourceFactId"]["sequence"] == std::to_string(Huge + 100)
            && json["view"]["commitStamp"]["revision"] == std::to_string(Huge + 1),
            "large history IDs or source versions lost JSON precision");
    request["after"] = Huge - 1;
    Require(nlohmann::json::parse(adapter.Handle(observer, request.dump()))["status"] == "InvalidArgument",
            "unsafe numeric history identity was accepted");
    const api::Rect far{source.Value.Knowledge.Discoveries[0].Location, 1, 1};
    const auto farView = service.OpenView(observer, far);
    auto discovery = Deliver(service, observer, service.Query(observer, farView.View->Id, {api::Topic::Knowledge, far}));
    auto exact = llm::ToJson(discovery)["page"]["discoveries"][0];
    Require(exact["ownerActor"] == std::to_string(Huge)
            && exact["location"]["x"] == std::to_string(far.Origin.X)
            && exact["location"]["y"] == std::to_string(far.Origin.Y), "knowledge owner or coordinate lost JSON precision");
}
} // namespace

int main() {
    try {
        KnowledgeAuthorizationAndRetention();
        FrozenHistoryAndGaps();
        DerivedMemoryNeedsDeliveredEvidence();
        ExactJsonIdentifiers();
        std::cout << "Knowledge view contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
