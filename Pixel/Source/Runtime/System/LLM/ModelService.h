#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pixel::system::llm {
using RequestId = uint64_t;
struct Request {
    RequestId Id = 0;
    std::string Instructions;
    std::string Input;
    size_t MaximumReplyBytes = 0;
};
enum class ReplyStatus { Completed, Unavailable, Cancelled };
struct Reply {
    RequestId Id = 0;
    ReplyStatus Status = ReplyStatus::Completed;
    std::string Text;
};

// Provider boundary: owned text and correlation IDs, with no world, actor,
// action or authorization types. Gameplay supplies and validates its protocol.
// Called by one host thread. TryStart/Poll/Cancel do not wait for remote work.
// A false TryStart retains nothing; providers enforce MaximumReplyBytes before
// retaining a reply. IDs are unique over an owner's lifetime. Cancel may race
// a completed reply, so consumers must discard replies for retired IDs.
// Close is idempotent, cancels work and promptly joins workers. Destruction
// must release the same resources. Network/configuration belongs to providers.
class ModelService {
  public:
    virtual ~ModelService() = default;
    virtual std::string_view DisplayName() const = 0;
    virtual bool TryStart(Request request) = 0;
    virtual std::optional<Reply> Poll() = 0;
    virtual void Cancel(RequestId id) = 0;
    virtual void Close() = 0;
};
} // namespace pixel::system::llm
