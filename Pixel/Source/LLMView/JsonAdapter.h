#pragma once
#include "ViewService.h"
#include <nlohmann/json.hpp>

namespace pixel::llm {
// These serializers accept owned, already authorized values. They never query
// domain state. In particular Response must come from ViewService::Deliver.
nlohmann::json ToJson(const Response &response);
nlohmann::json ToJson(const OpenResult &result);
nlohmann::json ToJson(const Capabilities &capabilities);
nlohmann::json ToJson(const api::ActionReceipt &receipt);

class JsonAdapter {
  public:
    static constexpr uint32_t MaximumRequestBytes = 16 * 1024;
    static constexpr uint32_t MaximumResponseBytes = 1024 * 1024;

    explicit JsonAdapter(ViewService &service) : Service(service) {}
    // Observer is bound by the host; JSON cannot register or authorize observers.
    // Query/ReadFacts are delivered immediately, without a second JSON cache.
    std::string Handle(ObserverId observer, std::string_view request);

  private:
    ViewService &Service;
};
} // namespace pixel::llm
