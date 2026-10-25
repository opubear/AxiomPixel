#include "Runtime/Gameplay/LLM/Services/ServiceLLM.h"
#include "Runtime/Gameplay/LLM/Services/ProposalProtocol.h"
#include <deque>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
namespace provider = system::llm;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
struct State {
    std::deque<provider::Request> Requests;
    std::deque<provider::Reply> Replies;
    std::vector<uint64_t> Cancelled;
    bool Busy = false;
    unsigned Closed = 0;
};
class Provider final : public provider::ModelService {
  public:
    explicit Provider(State &state) : Value(state) {}
    std::string_view DisplayName() const override { return "test provider"; }
    bool TryStart(provider::Request request) override {
        if (Value.Busy) return false;
        Value.Requests.push_back(std::move(request));
        return true;
    }
    std::optional<provider::Reply> Poll() override {
        if (Value.Replies.empty()) return {};
        auto reply = std::move(Value.Replies.front());
        Value.Replies.pop_front();
        return reply;
    }
    void Cancel(uint64_t id) override { Value.Cancelled.push_back(id); }
    void Close() override { ++Value.Closed; }
  private:
    State &Value;
};
llm::ModelInput Input(uint64_t id) {
    llm::ModelInput input;
    input.Id = id;
    input.Intent.Prayer = "Help this garden";
    input.Intent.Scope = {{10, 20}, 4, 4};
    return input;
}
constexpr auto Proposal = R"({"schema":"pixel.proposal/1","kind":"at_cell","x":"12","y":"22","deviceId":"0","explanation":"A suggestion"})";

void ServiceBoundary() {
    State state;
    {
        llm::service::ServiceLLM model(std::make_unique<Provider>(state));
        Check(model.DisplayName() == "test provider", "provider name lost");
        state.Busy = true;
        Check(!model.TryStart(Input(1)), "busy provider accepted a request");
        state.Busy = false;
        Check(model.TryStart(Input(1)) && model.TryStart(Input(2)) && !model.TryStart(Input(3)), "in-flight bound changed");
        const auto &request = state.Requests.front();
        Check(request.Id == 1 && request.Input == llm::service::ObservationJson(Input(1))
            && request.Instructions == llm::service::ProposalInstructions()
            && request.MaximumReplyBytes == llm::service::MaximumProposalBytes, "game protocol did not reach provider as bounded owned text");
        model.Cancel(1);
        Check(state.Cancelled == std::vector<uint64_t>{1} && model.TryStart(Input(3)), "cancel failed to release capacity");
        state.Replies.push_back({1, provider::ReplyStatus::Completed, Proposal});
        state.Replies.push_back({2, provider::ReplyStatus::Completed, Proposal});
        Check(!model.Poll(), "cancelled result became a proposal");
        const auto reply = model.Poll();
        Check(reply && reply->Id == 2 && reply->Status == llm::ModelStatus::Proposed
            && reply->Choice.Kind == llm::ProposalKind::AtCell && reply->Choice.Target == api::Cell{12, 22}, "valid game proposal changed");
        state.Replies.push_back({2, provider::ReplyStatus::Completed, Proposal});
        Check(!model.Poll(), "duplicate result was replayed");
        state.Replies.push_back({3, provider::ReplyStatus::Completed, "{malformed"});
        Check(model.Poll()->Status == llm::ModelStatus::Invalid, "malformed reply crossed game validation");
        Check(model.TryStart(Input(4)), "completion did not release capacity");
        state.Replies.push_back({4, provider::ReplyStatus::Unavailable, "provider-specific details"});
        Check(model.Poll()->Status == llm::ModelStatus::Unavailable, "provider error became an action");
        Check(model.TryStart(Input(5)), "failed reply did not release capacity");
        state.Replies.push_back({5, provider::ReplyStatus::Completed, std::string(llm::service::MaximumProposalBytes + 1, 'x')});
        Check(model.Poll()->Status == llm::ModelStatus::Invalid, "oversized provider reply bypassed validation");
        model.Close();
        model.Close();
        Check(!model.TryStart(Input(6)) && !model.Poll() && state.Closed == 1, "closed adapter retained work");
    }
    Check(state.Closed == 1, "destruction repeated close");
    State destruction;
    { llm::service::ServiceLLM model(std::make_unique<Provider>(destruction)); }
    Check(destruction.Closed == 1, "destruction failed to close the provider");
}
} // namespace
int main() {
    try { ServiceBoundary(); }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
