#pragma once
#include "Runtime/Gameplay/LLM/LLM.h"
#include "Runtime/Gameplay/GameAPI/WorldWillHost.h"
#include <deque>

namespace pixel::will {
using DecisionState = api::DecisionState;
const char *DecisionStateName(DecisionState state);
enum class RequestStatus { Accepted, Closed, Capacity, NotReady, InvalidGoal, ObservationDenied, ModelBusy };
struct RequestResult {
    RequestStatus Status = RequestStatus::NotReady;
    llm::DecisionId Id = 0;
    llm::Status Observation = llm::Status::Ok;
};
struct DecisionResult {
    llm::DecisionId Id = 0;
    DecisionState State = DecisionState::Thinking;
    api::CommitStamp ObservedAt;
    std::optional<api::ActionReceipt> Action;
    std::string Explanation;
};
struct Metrics { size_t Thinking = 0, Ready = 0, Completed = 0; uint64_t IgnoredReplies = 0; };

// Called at application service boundaries, outside GameSession::Tick. Only
// this host-thread object can turn proposals into current-state game actions.
class WorldWill {
  public:
    WorldWill(api::WorldWillHost &host, llm::LLM &model, std::chrono::milliseconds timeout = std::chrono::seconds(5));
    ~WorldWill();
    WorldWill(const WorldWill &) = delete;
    WorldWill &operator=(const WorldWill &) = delete;
    RequestResult Request(llm::Goal goal, llm::Clock::time_point now = llm::Clock::now());
    bool CancelPrayer(uint64_t durablePrayerId, const api::CommitStamp &observed);
    std::string_view ModelName() const { return Model.DisplayName(); }
    void Service(llm::Clock::time_point now = llm::Clock::now());
    void Close();
    const std::deque<DecisionResult> &Results() const { return Completed; }
    Metrics Statistics() const;

  private:
    struct Pending {
        llm::DecisionId Id = 0;
        api::CommitStamp ObservedAt;
        uint64_t Caller = 0, AuthorizationVersion = 0;
        llm::Goal Intent;
        api::ProgramRef Program;
        api::InterventionResources Resources;
        std::vector<api::DeviceObservation> Devices;
        llm::Clock::time_point Deadline;
        std::optional<llm::ModelReply> Reply;
        uint64_t PrayerId = 0;
        bool ModelCancelled = false, CancelRequested = false;
    };
    api::WorldWillHost &Host;
    llm::LLM &Model;
    llm::ViewService Views;
    llm::ObserverId Observer = 0;
    uint64_t BoundCaller = 0, IgnoredReplies = 0;
    std::chrono::milliseconds Timeout;
    llm::Clock::time_point LastService{};
    std::optional<llm::Clock::time_point> PausedAt;
    bool Stopped = false;
    std::vector<Pending> Active;
    std::deque<DecisionResult> Completed;
    void CheckTime(llm::Clock::time_point now);
    void Finish(size_t index, DecisionState state, std::optional<api::ActionReceipt> receipt = {});
    RequestResult Observe(llm::Goal goal, const api::WorldWillHostStatus &status, llm::ModelInput &input);
    std::optional<api::Action> ActionFor(const Pending &pending) const;
};
} // namespace pixel::will
