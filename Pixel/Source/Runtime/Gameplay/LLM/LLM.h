#pragma once
#include "Runtime/Gameplay/GameAPI/Actions.h"
#include "Runtime/Gameplay/GameAPI/WorldWillState.h"
#include "Runtime/Gameplay/LLMView/ViewService.h"
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace pixel::llm {
using DecisionId = uint64_t;
using Clock = std::chrono::steady_clock;
struct ModelLimits {
    static constexpr size_t InFlight = 2, Cells = 4096, Devices = 64;
    static constexpr size_t InputBytes = 512 * 1024, ExplanationBytes = 1024, Results = 32;
    static constexpr size_t PrayerBytes = api::WorldWillMemory::PrayerBytes;
};
struct Goal {
    api::Rect Scope;
    api::Cell Target;
    std::optional<uint64_t> Device;
    std::string Prayer;
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
enum class ModelStatus { Proposed, Unavailable, Invalid };
struct ModelReply {
    DecisionId Id = 0;
    ModelStatus Status = ModelStatus::Proposed;
    Proposal Choice;
    std::string Explanation; // Untrusted derived text, never a domain fact.
};
// Game decision contract for ServiceLLM and PseudoLLM. Generic providers use
// System/LLM/ModelService; game goals and proposals stay in this layer. The application
// owns one LLM; WorldWill borrows it to request, poll and cancel decisions.
// Exclusive host-thread client. Operations never wait for remote work. Close
// must cancel outstanding I/O/work and promptly join any workers; destruction
// must also release them. Close is idempotent and prevents further requests.
class LLM {
  public:
    virtual ~LLM() = default;
    virtual std::string_view DisplayName() const { return "Model"; }
    virtual bool TryStart(ModelInput input) = 0;
    virtual std::optional<ModelReply> Poll() = 0;
    virtual void Cancel(DecisionId id) = 0;
    virtual void Close() = 0;
};
} // namespace pixel::llm
