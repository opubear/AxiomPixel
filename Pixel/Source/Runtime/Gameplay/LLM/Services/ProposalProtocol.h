#pragma once
#include "Runtime/Gameplay/LLM/LLM.h"
#include <string_view>

namespace pixel::llm::service {
inline constexpr size_t MaximumProposalBytes = 16 * 1024;
inline constexpr size_t MaximumObservationBytes = 1024 * 1024;

// Provider-independent wire values. The adapter sees only the already
// authorized, owned input; it cannot query a session or supply action credentials.
std::string ObservationJson(const ModelInput &input);
std::string_view ProposalInstructions();
ModelReply ParseProposal(DecisionId id, std::string_view text);
} // namespace pixel::llm::service
