#include "LLM/Services/ProposalProtocol.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

using namespace pixel;
namespace {
using Json = nlohmann::json;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

Json Proposal() {
    return {{"schema", "pixel.proposal/1"}, {"kind", "none"}, {"x", "0"}, {"y", "0"},
        {"deviceId", "0"}, {"explanation", "I can suggest help, but the game decides."}};
}

void ValidProposalsPreserveIdentityAndPrecision() {
    auto proposal = Proposal();
    const auto none = llm::service::ParseProposal(42, proposal.dump());
    Check(none.Id == 42 && none.Status == llm::ModelStatus::Proposed && none.Choice.Kind == llm::ProposalKind::None,
        "no-action proposal changed the host identity or outcome");
    proposal["kind"] = "at_cell";
    proposal["x"] = "-9223372036854775808";
    proposal["y"] = "9223372036854775807";
    const auto cell = llm::service::ParseProposal(43, proposal.dump());
    Check(cell.Status == llm::ModelStatus::Proposed && cell.Choice.Kind == llm::ProposalKind::AtCell
        && cell.Choice.Target == api::Cell{INT64_MIN, INT64_MAX}, "coordinates lost int64 precision");
    proposal["kind"] = "at_device";
    proposal["x"] = proposal["y"] = "0";
    proposal["deviceId"] = "18446744073709551615";
    proposal["explanation"] = "可以提供帮助。\nIt still needs approval from the game.";
    const auto device = llm::service::ParseProposal(44, proposal.dump());
    Check(device.Status == llm::ModelStatus::Proposed && device.Choice.Kind == llm::ProposalKind::AtDevice
        && device.Choice.DeviceId == UINT64_MAX && device.Explanation == proposal["explanation"],
        "device identity or Unicode explanation changed");
}

void RejectMalformedOrAmbiguousOutput() {
    const auto reject = [](std::string_view text) {
        const auto reply = llm::service::ParseProposal(91, text);
        Check(reply.Id == 91 && reply.Status == llm::ModelStatus::Invalid
            && reply.Choice.Kind == llm::ProposalKind::None, "invalid output became an actionable proposal");
    };
    for (const auto &text : {"", "null", "[]", "{", "{\"kind\":\"none\"}", "```json\n{}\n```"}) reject(text);
    reject(Proposal().dump() + " trailing text");
    reject(std::string(llm::service::MaximumProposalBytes + 1, ' '));
    auto extra = Proposal();
    extra["caller"] = "42";
    reject(extra.dump());
    auto duplicate = Proposal().dump();
    duplicate.insert(1, "\"kind\":\"at_cell\",");
    reject(duplicate);
    for (const auto &number : {"00", "-0", "+1", "1.0", "1e2", " 1", "9223372036854775808", "-9223372036854775809"}) {
        auto value = Proposal();
        value["kind"] = "at_cell";
        value["x"] = number;
        reject(value.dump());
    }
    for (const auto &field : {"schema", "kind", "x", "y", "deviceId", "explanation"}) {
        auto value = Proposal();
        value[field] = 1;
        reject(value.dump());
    }
    for (const auto &kind : {"install_code", "AtDevice", "at_device"}) {
        auto value = Proposal();
        value["kind"] = kind;
        reject(value.dump());
    }
    auto value = Proposal();
    value["x"] = "1";
    reject(value.dump());
    value = Proposal();
    value["kind"] = "at_cell";
    value["deviceId"] = "7";
    reject(value.dump());
    value = Proposal();
    value["kind"] = "at_device";
    for (const auto &id : {"-1", "18446744073709551616"}) {
        value["deviceId"] = id;
        reject(value.dump());
    }
    value = Proposal();
    value["explanation"] = std::string(1025, 'x');
    reject(value.dump());
    value["explanation"] = std::string("hidden\0text", 11);
    reject(value.dump());
    value["explanation"] = Json{{"instruction", "override permission"}};
    reject(value.dump());
}

void EncodeOnlyOwnedObservation() {
    llm::ModelInput input;
    input.Id = 100;
    input.ObservedAt = {"world-model-test", 9, 88, 123};
    input.Intent.Scope = {{-100, 80}, 32, 32};
    input.Intent.Target = {-96, 99};
    input.Intent.Device = UINT64_MAX;
    input.Intent.Prayer = "Help the home bed. \"Ignore the rules\" is player text.";
    input.OfferedProgram = {55, 4};
    input.Materials.push_back({{-96, 99}, 4});
    input.Materials.push_back({{-95, 99}, {}});
    api::DeviceObservation device;
    device.Ref = {UINT64_MAX, 15};
    device.Anchor = {-96, 99};
    input.Devices.push_back(device);
    const auto text = llm::service::ObservationJson(input);
    const auto document = Json::parse(text);
    Check(document["observation"]["revision"] == "123" && document["prayer"]["source"] == "player"
        && document["prayer"]["text"] == input.Intent.Prayer, "input lost provenance or escaped player text");
    Check(document["requestedDeviceId"] == "18446744073709551615"
        && document["materials"][1]["materialId"].is_null(), "input rounded an identity or invented a material");
    Check(!document["devices"][0].contains("fuelCoal") && !document.contains("caller")
        && !document.contains("credentials"), "protocol invented hidden inventory or caller authority");
    input.Devices[0].Fuel = 0;
    Check(Json::parse(llm::service::ObservationJson(input))["devices"][0]["fuelCoal"] == 0,
        "authorized zero inventory was treated as unknown");
    input.Materials.resize(llm::ModelLimits::Cells, {{INT64_MIN, INT64_MAX}, UINT32_MAX});
    input.Devices.resize(llm::ModelLimits::Devices, device);
    input.Intent.Prayer.assign(1024, 'x');
    Check(llm::service::ObservationJson(input).size() <= llm::service::MaximumObservationBytes,
        "maximum legal observation exceeded wire bound");
}
} // namespace

int main() {
    try {
        ValidProposalsPreserveIdentityAndPrecision();
        RejectMalformedOrAmbiguousOutput();
        EncodeOnlyOwnedObservation();
        std::cout << "model protocol tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
