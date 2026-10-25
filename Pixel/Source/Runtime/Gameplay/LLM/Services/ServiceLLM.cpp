#include "ServiceLLM.h"
#include "ProposalProtocol.h"
#include <algorithm>
#include <stdexcept>

namespace pixel::llm::service {
ServiceLLM::ServiceLLM(std::unique_ptr<system::llm::ModelService> provider) : Provider(std::move(provider)) {
    if (!Provider) throw std::invalid_argument("model service requires a provider");
    Active.reserve(ModelLimits::InFlight);
}
ServiceLLM::~ServiceLLM() { Close(); }
std::string_view ServiceLLM::DisplayName() const { return Provider->DisplayName(); }

bool ServiceLLM::TryStart(ModelInput input) {
    auto observation = ObservationJson(input);
    if (Closed || Active.size() == ModelLimits::InFlight) return false;
    if (std::find(Active.begin(), Active.end(), input.Id) != Active.end())
        throw std::logic_error("model service received a duplicate active identity");
    system::llm::Request request{input.Id, std::string(ProposalInstructions()), std::move(observation), MaximumProposalBytes};
    if (!Provider->TryStart(std::move(request))) return false;
    Active.push_back(input.Id);
    return true;
}

std::optional<ModelReply> ServiceLLM::Poll() {
    if (Closed) return {};
    auto reply = Provider->Poll();
    if (!reply) return {};
    const auto active = std::find(Active.begin(), Active.end(), reply->Id);
    if (active == Active.end()) return {}; // Late, cancelled or duplicate result.
    Active.erase(active);
    if (reply->Status != system::llm::ReplyStatus::Completed)
        return ModelReply{reply->Id, ModelStatus::Unavailable, {}, "The model service is unavailable or cancelled."};
    return ParseProposal(reply->Id, reply->Text);
}

void ServiceLLM::Cancel(DecisionId id) {
    if (Closed || !std::erase(Active, id)) return;
    Provider->Cancel(id);
}

void ServiceLLM::Close() {
    if (Closed) return;
    Closed = true;
    Active.clear();
    Provider->Close();
}
} // namespace pixel::llm::service
