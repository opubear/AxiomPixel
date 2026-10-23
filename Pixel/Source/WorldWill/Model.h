#pragma once
#include "GameAPI/WorldWillHost.h"
#include "LLMView/ViewService.h"
#include <chrono>
#include <optional>
#include <string>

namespace pixel::will {
using DecisionId = uint64_t;
using Clock = std::chrono::steady_clock;
struct Limits {
    static constexpr size_t InFlight = 2, Cells = 4096, Devices = 64;
    static constexpr size_t InputBytes = 512 * 1024, ExplanationBytes = 1024, Results = 32;
};
struct Goal {
    api::Rect Scope;
    api::Cell Target;
    std::optional<uint64_t> Device;
};
// All values own their storage. No service handles, borrowed strings, pointers,
// source code or mutable domain objects cross the worker boundary.
struct ModelInput {
    DecisionId Id = 0;
    api::CommitStamp ObservedAt;
    Goal Intent;
    api::ProgramRef OfferedProgram;
    api::InterventionResources Resources;
    std::vector<llm::CellSample> Materials;
    std::vector<api::DeviceObservation> Devices;
    uint64_t RetainedBytes() const;
};
enum class ProposalKind { None, AtCell, AtDevice };
struct Proposal {
    ProposalKind Kind = ProposalKind::None;
    api::Cell Target;
    uint64_t DeviceId = 0;
};
enum class ModelStatus { Proposed, Unavailable };
struct ModelReply {
    DecisionId Id = 0;
    ModelStatus Status = ModelStatus::Proposed;
    Proposal Choice;
    std::string Explanation; // Untrusted derived text, never a domain fact.
};
// Exclusive host-thread client. Operations never wait for a decision. Close
// must cancel outstanding I/O/work and promptly join the adapter's workers.
class ModelPort {
  public:
    virtual ~ModelPort() = default;
    virtual bool TryStart(ModelInput input) = 0;
    virtual std::optional<ModelReply> Poll() = 0;
    virtual void Cancel(DecisionId id) = 0;
    virtual void Close() = 0;
};
} // namespace pixel::will
