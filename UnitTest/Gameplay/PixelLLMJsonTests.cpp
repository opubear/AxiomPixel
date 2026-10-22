#include "LLMView/JsonAdapter.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pixel;
namespace {
using Json = nlohmann::json;
constexpr int64_t Far = (int64_t{1} << 55) + 17;
constexpr uint64_t Identity = UINT64_MAX - 100;

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

Json ScopeJson(api::Rect scope) {
    return {{"origin", {{"x", std::to_string(scope.Origin.X)}, {"y", std::to_string(scope.Origin.Y)}}},
            {"width", scope.Width}, {"height", scope.Height}};
}

struct FixtureSource final : api::ObservationSource {
    api::CommitStamp Stamp{"json-fixture", Identity, Identity - 1, Identity - 2};
    api::Rect LastScope;
    uint32_t Captures = 0;
    bool ThrowOnCapture = false;

    api::ObservationResult CaptureObservation(api::Rect scope) override {
        ++Captures;
        if (ThrowOnCapture) throw std::logic_error("fixture domain invariant");
        LastScope = scope;
        api::ObservationSnapshot snapshot;
        snapshot.Stamp = Stamp;
        snapshot.Scope = scope;
        snapshot.FactHighWatermark = Identity + 6;
        snapshot.Paused = false;
        snapshot.Cells.resize(size_t(scope.Width) * scope.Height);
        snapshot.ResidentCellCount = uint32_t(snapshot.Cells.size());
        for (auto &cell : snapshot.Cells) {
            cell.MaterialId = 10;
            cell.AirKind = api::AirSampleKind::NotApplicable;
            cell.MaterialSampledAtRevision = Identity - 3;
            cell.WindSampledAtRevision = Identity - 4;
        }
        snapshot.Cells[0].MaterialId = 0;
        snapshot.Cells[0].AirKind = api::AirSampleKind::EmptyBackend;
        snapshot.Cells[0].AirVelocity = std::array<float, 2>{0, 0};
        if (snapshot.Cells.size() > 1) {
            snapshot.Cells[1].MaterialId.reset();
            snapshot.Cells[1].AirKind = api::AirSampleKind::NotResident;
            --snapshot.ResidentCellCount;
        }
        snapshot.Wind.Backend = api::WindKind::Hybrid;
        snapshot.Wind.Enabled = true;
        snapshot.Wind.Domain = scope;
        snapshot.Wind.SolverSpacingCells = std::array<double, 2>{8, 16};
        snapshot.Wind.AdaptiveMaximumEdgeCells = 32;
        snapshot.Wind.LocalObstacleCorrection = true;
        snapshot.Player = api::PlayerObservation{{scope.Origin, {.125, .875}}, {2, 4}, true};
        snapshot.Programs = {{Identity - 5, Identity - 6}, 1, 1, 2};
        snapshot.Projectiles.push_back({Identity - 7, Identity - 8, snapshot.Programs.Applied,
                                        {scope.Origin, {.25, .75}}, {12.5, -0.125}, .35});
        snapshot.ObjectSampledAtRevision = Identity - 9;
        return {api::ObservationStatus::Available, std::move(snapshot)};
    }

    api::ObservationServiceStatus ObservationService() const override { return {Stamp, true, false}; }

    api::FactReadResult ReadFacts(uint64_t epoch, uint64_t after, uint64_t high, uint32_t limit) const override {
        const api::RequestKey request{Identity, Identity - 10, Identity - 11};
        const api::ProgramEffectCause effect{{Identity, Identity - 12}, {Identity - 13, Identity - 14},
                                             Identity - 15, request, api::FactId{Identity, Identity - 16}};
        const api::Position position{{Far, -Far}, {.125, .875}};
        const std::array<api::FactPayload, 6> payloads{
            api::MaterialRemoved{{Far, -Far}, 10, 0},
            api::MaterialPlaced{{Far, -Far}, 0, 30},
            api::ProgramActivated{{Identity - 13, Identity - 14}, Identity - 17},
            api::WindConfigured{{api::WindKind::Empty, -Far}, {api::WindKind::Hybrid, Far}},
            api::WindPulseSubmitted{position, 3.25, {12.5, -6.25}, {Identity, Identity - 12}},
            api::MaterialRemovedInDisk{position, 4.5, 3, 2, 1}
        };
        api::FactReadResult result;
        result.WorldEpoch = epoch;
        result.HighWatermark = high;
        result.FirstAvailableSequence = Identity + 1;
        result.NextSequence = after;
        for (size_t i = 0; i < payloads.size(); ++i) {
            const auto sequence = Identity + i + 1;
            if (sequence <= after || sequence > high) continue;
            if (result.Facts.size() == limit) { result.HasMore = true; break; }
            result.Facts.push_back({{epoch, sequence}, Stamp,
                i % 2 ? api::FactCause(effect) : api::FactCause(request), payloads[i]});
            result.NextSequence = sequence;
        }
        return result;
    }
};

struct Harness {
    FixtureSource Source;
    llm::ViewService Service{Source};
    llm::JsonAdapter Adapter{Service};
    llm::ObserverId Observer = Service.RegisterObserver(llm::Policy::FullAccess());
    api::Rect Scope{{Far, -Far}, 4, 1};
    std::string View;

    Json Send(const Json &request) { return Json::parse(Adapter.Handle(Observer, request.dump())); }
    Json Open() {
        auto response = Send({{"method", "OpenView"}, {"scope", ScopeJson(Scope)}});
        Require(response["status"] == "Ok", "fixture view did not open");
        View = response["view"]["viewId"].get<std::string>();
        return response;
    }
    Json Query(std::string_view topic, uint32_t resolution = 1) const {
        return {{"method", "Query"}, {"viewId", View}, {"topic", topic},
                {"scope", ScopeJson(Scope)}, {"resolutionCells", resolution}};
    }
};

void LargeIntegerCoordinatesAndPagination() {
    Harness fixture;
    const auto opened = fixture.Open()["view"];
    Require(fixture.Source.LastScope == fixture.Scope
                && opened["scope"] == ScopeJson(fixture.Scope)
                && opened["commitStamp"]["worldEpoch"] == std::to_string(Identity)
                && opened["commitStamp"]["tick"] == std::to_string(Identity - 1)
                && opened["commitStamp"]["revision"] == std::to_string(Identity - 2)
                && opened["factHighWatermark"] == std::to_string(Identity + 6)
                && opened["expiresAtServiceFrame"].is_string(),
            "64-bit view identity, coordinates or commit stamp lost precision");
    auto request = fixture.Query("materials");
    request["pageSize"] = 2;
    request["cursor"] = "0";
    const auto first = fixture.Send(request)["page"];
    Require(first["cells"].size() == 2 && first["cells"][0]["materialId"] == 0
                && !first["cells"][1].contains("materialId")
                && first["cells"][0]["location"]["x"] == std::to_string(Far)
                && first["cells"][1]["location"]["x"] == std::to_string(Far + 1)
                && first["cells"][0]["sampledAtRevision"] == std::to_string(Identity - 3)
                && first["requestedCellCount"] == 2 && first["residentCellCount"] == 1
                && first["coverageFraction"] == .5 && first["availability"] == "partial"
                && first["reason"] == "not_resident" && first["source"] == api::ObservationSnapshot::MaterialSource
                && first["nextCursor"].is_string() && first["nextCursor"] != "0",
            "material paging omitted coverage, source, precision or missing-cell semantics");
    request["cursor"] = first["nextCursor"];
    const auto second = fixture.Send(request)["page"];
    Require(second["cells"][0]["location"]["x"] == std::to_string(Far + 2)
                && second["nextCursor"] == "0" && second["viewId"] == first["viewId"]
                && second["commitStamp"] == first["commitStamp"]
                && fixture.Service.Statistics().PendingDeliveries == 0,
            "JSON query did not consume its authorized delivery or preserve the frozen page identity");
    request["cursor"] = std::to_string(UINT64_MAX);
    Require(fixture.Send(request)["status"] == "InvalidCursor", "maximum uint64 cursor was not parsed exactly");

    const auto player = fixture.Send(fixture.Query("player"))["page"]["player"];
    Require(player["location"]["anchorCell"]["x"] == std::to_string(Far)
                && player["location"]["anchorCell"]["y"] == std::to_string(-Far)
                && player["location"]["offset"] == Json::array({.125, .875})
                && player["sizeCells"] == Json::array({2, 4}) && player["grounded"] == true,
            "player position flattened large anchors into an imprecise floating point coordinate");
    const auto runtime = fixture.Send(fixture.Query("runtime"))["page"];
    const auto &projectile = runtime["projectiles"][0];
    Require(runtime["paused"] == false && runtime["programs"]["runningRoots"] == 1
                && runtime["programs"]["applied"]["version"] == std::to_string(Identity - 6)
                && projectile["instanceId"] == std::to_string(Identity - 7)
                && projectile["projectileId"] == std::to_string(Identity - 8)
                && projectile["program"]["resourceId"] == std::to_string(Identity - 5)
                && projectile["location"]["anchorCell"]["y"] == std::to_string(-Far)
                && projectile["location"]["offset"] == Json::array({.25, .75})
                && projectile["velocityCellsPerSecond"] == Json::array({12.5, -.125})
                && projectile["radiusCells"] == .35,
            "runtime serialization omitted a projectile or program observation field");
    const auto programs = fixture.Send(fixture.Query("programs"))["page"];
    Require(programs["unit"] == "count" && programs["programs"]["projectiles"] == 1
                && programs["programs"]["pending"] == 2 && !programs.contains("projectiles"),
            "program summary fabricated an empty projectile observation");
}

void StrictInputsStayAtTheBoundary() {
    Harness fixture;
    const std::vector<std::string> malformed{
        "", "null", "[]", "{", "{} {}", R"({"method":1})", R"({"method":"RegisterObserver"})",
        R"({"method":"DescribeCapabilities","observerId":"1"})",
        R"({"method":"DescribeCapabilities","policy":{"facts":true}})",
        R"({"method":"DescribeCapabilities","method":"DescribeCapabilities"})",
        R"({"method":"OpenView","scope":{"origin":{"x":"0","x":"1","y":"0"},"width":1,"height":1}})",
        R"({"method":"OpenView","scope":{"origin":{"x":"0","y":"0"},"width":1e10000,"height":1}})",
        std::string(32, '[') + std::string(32, ']')
    };
    for (const auto &request : malformed)
        Require(Json::parse(fixture.Adapter.Handle(fixture.Observer, request)) == Json{{"status", "InvalidArgument"}},
                "malformed, duplicate or unauthorized JSON request was accepted");
    const auto tooLong = std::string(llm::JsonAdapter::MaximumRequestBytes + 1, ' ');
    Require(Json::parse(fixture.Adapter.Handle(fixture.Observer, tooLong))["status"] == "LimitExceeded",
            "JSON request byte limit was not enforced");
    Require(fixture.Source.Captures == 0, "invalid JSON caused domain capture");

    const std::vector<Json> invalidCoordinates{0, 0.0, nullptr, true, "", "+1", "01", "-0", " 1", "1 ",
        "1.0", "1e2", "9223372036854775808", "-9223372036854775809"};
    for (const auto &coordinate : invalidCoordinates) {
        auto scope = ScopeJson(fixture.Scope);
        scope["origin"]["x"] = coordinate;
        Require(fixture.Send({{"method", "OpenView"}, {"scope", scope}})["status"] == "InvalidArgument",
                "signed coordinate did not require canonical int64 decimal text");
    }
    fixture.Open();
    const std::vector<Json> invalidUnsigned{0, 1.0, nullptr, true, "", "-1", "+1", "01", "1.0", "1e2",
                                          "18446744073709551616"};
    for (const auto &number : invalidUnsigned) {
        auto query = fixture.Query("materials");
        query["cursor"] = number;
        Require(fixture.Send(query)["status"] == "InvalidArgument", "cursor accepted noncanonical uint64 JSON");
        query.erase("cursor");
        query["viewId"] = number;
        Require(fixture.Send(query)["status"] == "InvalidArgument", "viewId accepted noncanonical uint64 JSON");
    }
    const std::vector<Json> invalidCounts{-1, 1.0, true, nullptr, "1", uint64_t(UINT32_MAX) + 1};
    for (const auto &count : invalidCounts) {
        auto query = fixture.Query("materials");
        query["pageSize"] = count;
        Require(fixture.Send(query)["status"] == "InvalidArgument", "uint32 page size accepted a narrowing conversion");
    }
    for (const auto *field : {"observerId", "policy", "unexpected"}) {
        auto query = fixture.Query("materials");
        query[field] = 1;
        Require(fixture.Send(query)["status"] == "InvalidArgument", "unknown request field was silently ignored");
    }
    auto query = fixture.Query("materials");
    query["topic"] = "Materials";
    Require(fixture.Send(query)["status"] == "InvalidArgument", "topic parser did not enforce the public catalogue");
    query = fixture.Query("materials");
    query["scope"]["origin"]["z"] = "0";
    Require(fixture.Send(query)["status"] == "InvalidArgument", "unknown nested scope field was ignored");

    fixture.Source.ThrowOnCapture = true;
    bool propagated = false;
    try { fixture.Open(); }
    catch (const std::logic_error &error) { propagated = std::string(error.what()) == "fixture domain invariant"; }
    Require(propagated, "JSON adapter swallowed a domain invariant failure as malformed input");
}

void WindMetadataAndUnsupportedTopics() {
    Harness fixture;
    fixture.Open();
    const auto fine = fixture.Send(fixture.Query("air_velocity"))["page"];
    Require(fine["source"] == api::ObservationSnapshot::WindSource && fine["unit"] == "cell/second"
                && fine["wind"]["backendKind"] == "hybrid" && fine["wind"]["enabled"] == true
                && fine["wind"]["contractVersion"] == 1 && fine["wind"]["domain"] == ScopeJson(fixture.Scope)
                && fine["wind"]["solverSpacingCells"] == Json::array({8, 16})
                && fine["wind"]["adaptiveMaximumEdgeCells"] == 32 && fine["wind"]["localObstacleCorrection"] == true
                && fine["cells"][0]["airKind"] == "empty_backend"
                && fine["cells"][0]["velocityCellsPerSecond"] == Json::array({0, 0})
                && fine["cells"][1]["airKind"] == "not_resident"
                && !fine["cells"][1].contains("velocityCellsPerSecond")
                && fine["cells"][2]["airKind"] == "not_applicable"
                && !fine["cells"][2].contains("velocityCellsPerSecond"),
            "fine wind JSON confused unknown or non-air samples with known zero wind");
    const auto coarse = fixture.Send(fixture.Query("air_velocity", 16))["page"];
    const auto &summary = coarse["windSummaries"][0];
    Require(summary["scope"] == ScopeJson(fixture.Scope) && summary["requestedCellCount"] == 4
                && summary["residentCellCount"] == 3 && summary["airSampleCount"] == 1
                && summary["nonAirCount"] == 2 && summary["unknownCellCount"] == 1
                && summary["emptyBackendSampleCount"] == 1 && summary["outsideDomainSampleCount"] == 0
                && summary["disabledSampleCount"] == 0 && summary["coverageFraction"] == .75
                && summary["meanVelocityCellsPerSecond"] == Json::array({0, 0})
                && summary["availability"] == "partial"
                && summary["oldestSampledAtRevision"] == std::to_string(Identity - 4)
                && summary["newestSampledAtRevision"] == std::to_string(Identity - 4),
            "wind summary lost sample denominator, reason counts or sampling revisions");
    auto nonAir = fixture.Query("air_velocity", 16);
    nonAir["scope"] = ScopeJson({{Far + 2, -Far}, 2, 1});
    const auto emptyMean = fixture.Send(nonAir)["page"]["windSummaries"][0];
    Require(emptyMean["availability"] == "not_applicable" && !emptyMean.contains("meanVelocityCellsPerSecond"),
            "zero air samples fabricated a zero mean velocity");

    const auto capabilities = fixture.Send({{"method", "DescribeCapabilities"}});
    Require(capabilities["limits"]["maximumObservers"] == 3
                && capabilities["limits"]["maximumCursorsPerView"] == 64
                && capabilities["limits"]["maximumPendingDeliveriesPerView"] == 1
                && capabilities["limits"]["maximumViewBytes"] == "1048576"
                && capabilities["limits"]["leaseServiceFrames"] == "120"
                && capabilities["limits"]["maximumRequestBytes"] == llm::JsonAdapter::MaximumRequestBytes
                && capabilities["topics"][1]["resolutionCells"] == Json::array({1, 16}),
            "capability catalogue omitted enforced limits or supported wind resolutions");
    for (const auto *topic : {"temperature", "ecology", "items", "devices", "knowledge"}) {
        const auto response = fixture.Send(fixture.Query(topic));
        const auto &page = response["page"];
        Require(page["availability"] == "unsupported" && page["reason"] == "unsupported"
                    && !page.contains("cells") && !page.contains("programs") && !page.contains("coverageFraction")
                    && !page.contains("oldestSampledAtRevision") && !page.contains("source"),
                "unsupported topic was serialized as fabricated zero observations");
    }
}

void FactsPreserveEveryCauseAndPayload() {
    Harness fixture;
    fixture.Open();
    Json request{{"method", "ReadFacts"}, {"viewId", fixture.View}, {"after", std::to_string(Identity)}};
    const auto response = fixture.Send(request);
    const auto facts = response["facts"];
    Require(response["view"]["viewId"] == fixture.View
                && response["view"]["commitStamp"]["revision"] == std::to_string(Identity - 2)
                && response["view"]["factHighWatermark"] == std::to_string(Identity + 6),
            "fact response omitted its frozen view descriptor");
    Require(facts["status"] == "Available" && facts["worldEpoch"] == std::to_string(Identity)
                && facts["highWatermark"] == std::to_string(Identity + 6)
                && facts["firstAvailableSequence"] == std::to_string(Identity + 1)
                && facts["nextSequence"] == std::to_string(Identity + 6)
                && facts["hasMore"] == false && facts["facts"].size() == 6,
            "fact metadata lost uint64 precision or bounded-history cursor state");
    const auto &values = facts["facts"];
    Require(values[0]["id"]["sequence"] == std::to_string(Identity + 1)
                && values[0]["committedAt"]["revision"] == std::to_string(Identity - 2)
                && values[0]["cause"]["kind"] == "request"
                && values[0]["cause"]["request"]["caller"] == std::to_string(Identity - 10)
                && values[0]["cause"]["request"]["sequence"] == std::to_string(Identity - 11),
            "fact request cause or complete commit stamp was omitted");
    const auto &cause = values[1]["cause"];
    Require(cause["kind"] == "program_effect" && cause["instance"]["worldEpoch"] == std::to_string(Identity)
                && cause["instance"]["instanceId"] == std::to_string(Identity - 12)
                && cause["program"]["resourceId"] == std::to_string(Identity - 13)
                && cause["program"]["version"] == std::to_string(Identity - 14)
                && cause["emissionSequence"] == std::to_string(Identity - 15)
                && cause["activationRequest"] == values[0]["cause"]["request"]
                && cause["parentFact"]["sequence"] == std::to_string(Identity - 16),
            "program effect lost activation, program identity, emission or parent causality");
    Require(values[0]["payload"]["kind"] == "MaterialRemoved" && values[0]["payload"]["oldMaterial"] == 10
                && values[0]["payload"]["newMaterial"] == 0
                && values[0]["payload"]["target"]["x"] == std::to_string(Far)
                && values[1]["payload"]["kind"] == "MaterialPlaced" && values[1]["payload"]["newMaterial"] == 30
                && values[2]["payload"]["kind"] == "ProgramActivated"
                && values[2]["payload"]["activationId"] == std::to_string(Identity - 17)
                && values[3]["payload"]["kind"] == "WindConfigured"
                && values[3]["payload"]["before"]["boundaryY"] == std::to_string(-Far)
                && values[3]["payload"]["after"]["boundaryY"] == std::to_string(Far)
                && values[3]["payload"]["after"]["backendKind"] == "hybrid",
            "material, activation or wind configuration fact payload was incomplete");
    const auto &pulse = values[4]["payload"];
    const auto &disk = values[5]["payload"];
    Require(pulse["kind"] == "WindPulseSubmitted" && pulse["radiusCells"] == 3.25
                && pulse["center"]["anchorCell"]["y"] == std::to_string(-Far)
                && pulse["center"]["offset"] == Json::array({.125, .875})
                && pulse["requestedDeltaVelocityCellsPerSecond"] == Json::array({12.5, -6.25})
                && pulse["instance"]["instanceId"] == std::to_string(Identity - 12)
                && disk["kind"] == "MaterialRemovedInDisk" && disk["radiusCells"] == 4.5
                && disk["center"] == pulse["center"] && disk["changed"] == 3
                && disk["blocked"] == 2 && disk["notReady"] == 1,
            "effect fact payload lost precise position, submitted velocity or material outcomes");
    request["limit"] = 2;
    const auto first = fixture.Send(request)["facts"];
    Require(first["facts"].size() == 2 && first["hasMore"] == true
                && first["nextSequence"] == std::to_string(Identity + 2)
                && fixture.Service.Statistics().PendingDeliveries == 0,
            "fact JSON paging retained an undelivered response or lost cursor continuity");
}

void PermissionsDoNotLeakAndCloseReleases() {
    Harness fixture;
    fixture.Open();
    llm::Policy materials;
    materials.Topics[size_t(api::Topic::Materials)] = true;
    fixture.Service.SetPolicy(fixture.Observer, materials);
    Require(fixture.Send(fixture.Query("materials")) == Json{{"status", "ViewRevoked"}},
            "revoked view leaked an old JSON page");
    const auto opened = fixture.Open();
    Require(!opened["view"].contains("factHighWatermark"), "fact-denied observer learned the fact stream watermark");
    const auto capabilities = fixture.Send({{"method", "DescribeCapabilities"}});
    Require(capabilities["topics"].size() == 1 && capabilities["topics"][0]["topic"] == "materials"
                && capabilities["facts"] == false,
            "capability catalogue disclosed hidden topics");
    Require(fixture.Send(fixture.Query("runtime")) == Json{{"status", "PermissionDenied"}}
                && fixture.Send({{"method", "ReadFacts"}, {"viewId", fixture.View}, {"after", "0"}})
                    == Json{{"status", "PermissionDenied"}},
            "denial disclosed hidden program counts or facts");
    Require(Json::parse(fixture.Adapter.Handle(0, R"({"method":"DescribeCapabilities"})"))
                == Json{{"status", "PermissionDenied"}},
            "unregistered observer received a capability catalogue");

    llm::Response malicious;
    malicious.Result = llm::Status::PermissionDenied;
    malicious.Page.emplace();
    malicious.Page->Programs = api::ProgramObservation{{1, 2}, 90, 91, 92};
    malicious.Facts = fixture.Source.ReadFacts(Identity, Identity, Identity + 6, 256);
    Require(llm::ToJson(malicious) == Json{{"status", "PermissionDenied"}},
            "serializer disclosed fields attached to a denied DTO");
    Require(fixture.Send({{"method", "CloseView"}, {"viewId", fixture.View}}) == Json{{"status", "Ok"}}
                && fixture.Service.Statistics().ActiveViews == 0
                && fixture.Send(fixture.Query("materials")) == Json{{"status", "ViewExpired"}},
            "close did not release the view or reject subsequent JSON requests");
}

void ActionReceiptUsesTheSameIntegerContract() {
    api::ActionReceipt receipt;
    receipt.Key = {Identity, Identity - 1, Identity - 2};
    receipt.Status = api::ActionStatus::Applied;
    receipt.ResolvedAt = {"receipt-world", Identity, Identity - 3, Identity - 4};
    receipt.Facts = {Identity, Identity - 5, 6};
    receipt.Result = api::ActivationResult{Identity, Identity - 6, {Identity - 7, Identity - 8},
                                          Identity - 9, Identity - 10, Identity - 11};
    receipt.Message = "applied";
    auto json = llm::ToJson(receipt);
    Require(json["key"]["worldEpoch"] == std::to_string(Identity)
                && json["key"]["caller"] == std::to_string(Identity - 1)
                && json["key"]["sequence"] == std::to_string(Identity - 2)
                && json["status"] == "Applied" && json["reason"] == "None"
                && json["resolvedAt"]["tick"] == std::to_string(Identity - 3)
                && json["facts"]["firstSequence"] == std::to_string(Identity - 5)
                && json["facts"]["count"] == 6 && json["message"] == "applied"
                && json["result"]["activationId"] == std::to_string(Identity - 6)
                && json["result"]["program"]["version"] == std::to_string(Identity - 8)
                && json["result"]["runningRoots"] == std::to_string(Identity - 9)
                && json["result"]["projectiles"] == std::to_string(Identity - 10)
                && json["result"]["pending"] == std::to_string(Identity - 11),
            "action receipt encoded uint64 fields as lossy JSON numbers");
    receipt.Result = api::MaterialResult{{Far, -Far}, {}, 0, 1, 2, 3};
    json = llm::ToJson(receipt);
    Require(json["result"]["target"]["x"] == std::to_string(Far)
                && !json["result"].contains("before") && json["result"]["after"] == 0
                && json["result"]["changed"] == 1 && json["result"]["blocked"] == 2
                && json["result"]["notReady"] == 3,
            "material receipt lost optional-state or integer coordinate semantics");
    receipt.Result = api::WindResult{api::WindKind::Lbm, -Far};
    json = llm::ToJson(receipt);
    Require(json["result"]["boundaryY"] == std::to_string(-Far) && json["result"]["backendKind"] == "lbm",
            "wind action receipt lost backend or signed boundary precision");
}
} // namespace

int main() {
    try {
        LargeIntegerCoordinatesAndPagination();
        StrictInputsStayAtTheBoundary();
        WindMetadataAndUnsupportedTopics();
        FactsPreserveEveryCauseAndPayload();
        PermissionsDoNotLeakAndCloseReleases();
        ActionReceiptUsesTheSameIntegerContract();
        std::cout << "LLM JSON boundary contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
