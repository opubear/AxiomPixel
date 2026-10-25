#include "Runtime/Gameplay/LLM/PseudoLLM.h"
#include "Runtime/Gameplay/LLM/DecisionAPI.h"
#include "Runtime/Gameplay/LLM/Services/ProposalProtocol.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/Gameplay/CommittedWorldView.h"
#include "Runtime/Gameplay/WorldWill/WorldWill.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>

using namespace pixel;
namespace {
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

llm::ModelInput Input(uint64_t id) {
    llm::ModelInput input;
    input.Id = id;
    input.Intent = {{{80, 80}, 40, 40}, {100, 100}, {}, "Please help."};
    input.Materials.push_back({{100, 100}, 1});
    return input;
}

llm::ModelReply Decide(llm::LLM &model, llm::ModelInput input) {
    Check(model.TryStart(std::move(input)), "pseudo request unexpectedly refused");
    auto reply = model.Poll();
    Check(reply.has_value() && !model.Poll(), "pseudo request did not produce exactly one reply");
    return std::move(*reply);
}

void CheckWireEquivalent(const llm::ModelReply &native) {
    const auto &choice = native.Choice;
    const char *kind = choice.Kind == llm::ProposalKind::AtCell ? "at_cell"
        : choice.Kind == llm::ProposalKind::AtDevice ? "at_device" : "none";
    nlohmann::json wire{{"schema", "pixel.proposal/1"}, {"kind", kind},
        {"x", std::to_string(choice.Target.X)}, {"y", std::to_string(choice.Target.Y)},
        {"deviceId", std::to_string(choice.DeviceId)}, {"explanation", native.Explanation}};
    const auto decoded = llm::service::ParseProposal(native.Id, wire.dump());
    Check(decoded.Id == native.Id && decoded.Status == native.Status
        && decoded.Choice.Kind == choice.Kind && decoded.Choice.Target == choice.Target
        && decoded.Choice.DeviceId == choice.DeviceId && decoded.Explanation == native.Explanation,
        "provider and native calls have different API results");
}

void RandomCompositionsAndSharedAPI() {
    llm::PseudoLLM first(42), replay(42);
    size_t wait = 0, cell = 0, device = 0;
    for (uint64_t id = 1; id <= 64; ++id) {
        auto input = Input(id);
        if (id % 2) {
            input.Intent.Device = 17;
            api::DeviceObservation observed;
            observed.Ref = {17, 2};
            observed.Anchor = {100, 100};
            input.Devices.push_back(observed);
        }
        const auto a = Decide(first, input), b = Decide(replay, input);
        Check(a.Choice.Kind == b.Choice.Kind && a.Explanation == b.Explanation,
            "same seed and requests did not reproduce decisions");
        Check(a.Id == id && a.Status == llm::ModelStatus::Proposed
            && a.Explanation.starts_with("PseudoLLM randomly chose")
            && a.Explanation.find('\n') != std::string::npos, "decision did not execute its composed API calls");
        if (a.Choice.Kind == llm::ProposalKind::None) ++wait;
        else if (a.Choice.Kind == llm::ProposalKind::AtCell) {
            ++cell;
            Check(a.Choice.Target == input.Intent.Target, "cell decision invented a target");
        } else {
            ++device;
            Check(a.Choice.DeviceId == 17, "device decision invented an identity");
        }
        CheckWireEquivalent(a);
    }
    Check(wait && cell && device, "seeded sequence did not exercise both decisions and target kinds");

    llm::DecisionAPI calls(100);
    calls.Explain("first call");
    calls.Explain("second call");
    calls.ProposeAtDevice(17);
    const auto reply = std::move(calls).Finish();
    Check(reply.Explanation == "first call\nsecond call", "composed explanation order changed");
    CheckWireEquivalent(reply);
}

void MissingTargetsAndLifecycle() {
    llm::PseudoLLM model(42);
    for (uint64_t id = 1; id <= 32; ++id) {
        auto input = Input(id);
        input.Materials.front().MaterialId.reset();
        if (id % 2) input.Intent.Device = 99;
        Check(Decide(model, input).Choice.Kind == llm::ProposalKind::None,
            "pseudo LLM invented an unknown cell or unobserved device");
    }
    Check(model.TryStart(Input(33)) && model.TryStart(Input(34)) && !model.TryStart(Input(35)),
        "undrained pseudo completions exceeded capacity");
    model.Cancel(33);
    Check(model.TryStart(Input(35)), "cancel did not release capacity");
    Check(model.Poll()->Id == 34 && model.Poll()->Id == 35 && !model.Poll(),
        "cancelled reply escaped or completion order changed");
    Check(model.TryStart(Input(36)), "close fixture failed");
    model.Close();
    model.Close();
    Check(!model.Poll() && !model.TryStart(Input(37)), "closed model retained or restarted work");
}

uint32_t AidSeed() {
    for (uint32_t seed = 0; seed < 64; ++seed) {
        llm::PseudoLLM model(seed);
        if (Decide(model, Input(1)).Choice.Kind == llm::ProposalKind::AtCell) return seed;
    }
    throw std::runtime_error("could not find a seeded aid fixture");
}

void GameControlsExecution() {
    GameSession session;
    auto compiled = formula::process::Compile("pseudo-aid",
        "process 1; emit field(cells(4), acceleration(100), acceleration(0), ticks(1200)); "
        "await result within ticks(1); wait ticks(1000);");
    Check(bool(session.ApplyProcess(NativeCaller::Driver, std::get<formula::process::ProgramArtifact>(compiled))),
        "could not configure aid");
    llm::PseudoLLM model(AidSeed());
    will::WorldWill decisions(session, model);
    const auto request = decisions.Request(Input(1).Intent);
    Check(request.Status == will::RequestStatus::Accepted && session.GetAuthority().Resources().Available == 4,
        "pseudo request bypassed the game controller");
    Check(session.Control(NativeCaller::Driver, api::SetPaused{true}).Succeeded(), "pause game failed");
    decisions.Service();
    Check(decisions.Results().empty() && decisions.Statistics().Thinking == 1
        && session.GetAuthority().Resources().Available == 4, "paused pseudo proposal executed");
    Check(session.Control(NativeCaller::Driver, api::SetPaused{false}).Succeeded(), "unpause failed");
    decisions.Service();
    Check(decisions.Results().back().State == will::DecisionState::Applied
        && decisions.Results().back().Action->Facts.Count > 0
        && session.GetAuthority().Resources().Available == 3 && session.GetWorld().GetForce(100, 100).X == 100,
        "pseudo aid did not produce real game receipts and effects");

    const auto cancel = decisions.Request(Input(2).Intent);
    Check(cancel.Status == will::RequestStatus::Accepted, "cancel request failed");
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {80, 80, 40, 40});
    Check(!view->Gameplay().WorldWill.Records.empty(), "cancel prayer missing from published archive");
    const auto record = view->Gameplay().WorldWill.Records.back();
    Check(decisions.CancelPrayer(record.Id, record.ObservedAt), "pseudo request could not be cancelled");
    decisions.Service();
    Check(decisions.Results().back().State == will::DecisionState::Cancelled
        && session.GetAuthority().Resources().Available == 3, "cancelled pseudo proposal executed");

    Check(decisions.Request(Input(3).Intent).Status == will::RequestStatus::Accepted, "revocation request failed");
    Check(session.SetWorldWillCapabilities(NativeCaller::Driver, {false, true}).Succeeded(), "revoke failed");
    decisions.Service();
    Check(decisions.Results().back().State == will::DecisionState::ViewRevoked
        && session.GetAuthority().Resources().Available == 3, "pseudo proposal bypassed observation revocation");
}
} // namespace

int main() {
    try {
        RandomCompositionsAndSharedAPI();
        MissingTargetsAndLifecycle();
        GameControlsExecution();
        std::cout << "pseudo LLM and shared decision API contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
