#pragma once
#include "LLM.h"
#include <deque>
#include <random>

namespace pixel::llm {
// Offline game fallback. Each bounded decision is a native function calling
// DecisionAPI; no thread, I/O, credentials or live game reference is needed.
class PseudoLLM final : public LLM {
  public:
    PseudoLLM();
    explicit PseudoLLM(uint32_t seed);
    std::string_view DisplayName() const override { return "PseudoLLM (random offline decisions)"; }
    bool TryStart(ModelInput input) override;
    std::optional<ModelReply> Poll() override;
    void Cancel(DecisionId id) override;
    void Close() override;

  private:
    std::mt19937 Random;
    std::deque<ModelReply> Replies;
    bool Closed = false;
};
} // namespace pixel::llm
