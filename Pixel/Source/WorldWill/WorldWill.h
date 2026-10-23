#pragma once
#include "Model.h"
#include <deque>

namespace pixel::will {
enum class DecisionState {
    Thinking, Ready, Applied, Rejected, NoAction, TimedOut, StaleEpoch,
    ViewRevoked, ModelUnavailable, InvalidProposal, Closed
};
const char *DecisionStateName(DecisionState state);
enum class RequestStatus { Accepted, Closed, Capacity, NotReady, InvalidGoal, ObservationDenied, ModelBusy };
struct RequestResult {
    RequestStatus Status = RequestStatus::NotReady;
    DecisionId Id = 0;
    llm::Status Observation = llm::Status::Ok;
};
struct DecisionResult {
    DecisionId Id = 0;
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
    WorldWill(api::WorldWillHost &host, ModelPort &model, std::chrono::milliseconds timeout = std::chrono::seconds(5));
    ~WorldWill();
    WorldWill(const WorldWill &) = delete;
    WorldWill &operator=(const WorldWill &) = delete;
    RequestResult Request(Goal goal, Clock::time_point now = Clock::now());
    void Service(Clock::time_point now = Clock::now());
    void Close();
    const std::deque<DecisionResult> &Results() const { return Completed; }
    Metrics Statistics() const;

  private:
    struct Pending {
        DecisionId Id = 0;
        api::CommitStamp ObservedAt;
        uint64_t Caller = 0, AuthorizationVersion = 0;
        Goal Intent;
        api::ProgramRef Program;
        api::InterventionResources Resources;
        std::vector<api::DeviceObservation> Devices;
        Clock::time_point Deadline;
        std::optional<ModelReply> Reply;
    };
    api::WorldWillHost &Host;
    ModelPort &Model;
    llm::ViewService Views;
    llm::ObserverId Observer = 0;
    uint64_t BoundCaller = 0, IgnoredReplies = 0;
    std::chrono::milliseconds Timeout;
    Clock::time_point LastService{};
    bool Stopped = false;
    std::vector<Pending> Active;
    std::deque<DecisionResult> Completed;
    void CheckTime(Clock::time_point now);
    void Finish(size_t index, DecisionState state, std::optional<api::ActionReceipt> receipt = {});
    RequestResult Observe(Goal goal, const api::WorldWillHostStatus &status, ModelInput &input);
    std::optional<api::Action> ActionFor(const Pending &pending) const;
};
} // namespace pixel::will
