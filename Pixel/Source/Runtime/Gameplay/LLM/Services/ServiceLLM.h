#pragma once
#include "Runtime/Gameplay/LLM/LLM.h"
#include "Runtime/System/LLM/ModelService.h"
#include <memory>
#include <vector>

namespace pixel::llm::service {
// Game adapter for a generic System provider. WorldWill still owns observation
// authorization, timeouts and the only path from a proposal to an actual action.
class ServiceLLM final : public LLM {
  public:
    explicit ServiceLLM(std::unique_ptr<system::llm::ModelService> provider);
    ~ServiceLLM() override;
    std::string_view DisplayName() const override;
    bool TryStart(ModelInput input) override;
    std::optional<ModelReply> Poll() override;
    void Cancel(DecisionId id) override;
    void Close() override;

  private:
    std::unique_ptr<system::llm::ModelService> Provider;
    std::vector<DecisionId> Active;
    bool Closed = false;
};
} // namespace pixel::llm::service
