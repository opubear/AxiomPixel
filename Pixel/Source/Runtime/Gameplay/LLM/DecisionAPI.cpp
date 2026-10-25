#include "DecisionAPI.h"
#include <stdexcept>
#include <utility>

namespace pixel::llm {
DecisionAPI::DecisionAPI(DecisionId id) : Reply{id} {
    if (!id) throw std::invalid_argument("decision requires its locally assigned identity");
}

void DecisionAPI::Explain(std::string_view text) {
    if (Chosen || Finished) throw std::logic_error("explanation must precede the terminal proposal");
    const size_t separator = Reply.Explanation.empty() ? 0 : 1;
    if (Reply.Explanation.size() + separator + text.size() > ModelLimits::ExplanationBytes)
        throw std::length_error("decision explanation exceeded its bound");
    if (separator) Reply.Explanation += '\n';
    Reply.Explanation += text;
}

void DecisionAPI::Choose(Proposal proposal) {
    if (Chosen || Finished) throw std::logic_error("decision already chose a terminal proposal");
    Reply.Choice = proposal;
    Chosen = true;
}

void DecisionAPI::NoAction() { Choose({}); }

void DecisionAPI::ProposeAtCell(api::Cell target) { Choose({ProposalKind::AtCell, target, 0}); }

void DecisionAPI::ProposeAtDevice(uint64_t deviceId) {
    if (!deviceId) throw std::invalid_argument("decision device identity must be nonzero");
    Choose({ProposalKind::AtDevice, {}, deviceId});
}

ModelReply DecisionAPI::Finish() && {
    if (!Chosen || Finished) throw std::logic_error("decision must finish exactly once after choosing a proposal");
    Finished = true;
    return std::move(Reply);
}
} // namespace pixel::llm
