#pragma once
#include "LLM.h"

namespace pixel::llm {
// Both native decisions and decoded model calls use this API. It only builds
// a proposal: WorldWill owns authorization, timing and the eventual game action.
// A decision may explain in several steps, then choose one terminal proposal.
class DecisionAPI {
  public:
    explicit DecisionAPI(DecisionId id);
    void Explain(std::string_view text);
    void NoAction();
    void ProposeAtCell(api::Cell target);
    void ProposeAtDevice(uint64_t deviceId);
    ModelReply Finish() &&;

  private:
    ModelReply Reply;
    bool Chosen = false, Finished = false;
    void Choose(Proposal proposal);
};
} // namespace pixel::llm
