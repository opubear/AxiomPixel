#pragma once
#include "Model.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace pixel::will {
struct FakePlan {
    std::chrono::milliseconds Delay{0};
    bool HoldUntilReleased = false;
    bool DuplicateReply = false;
    bool Unavailable = false;
};
struct FakeStatistics {
    size_t Pending = 0, Replies = 0, Plans = 0;
    uint64_t Completed = 0;
    bool Closed = false;
};

// Offline adapter. One worker schedules bounded requests independently, so a
// held/slow request does not prevent a later request from completing first.
class FakeModel final : public ModelPort {
  public:
    FakeModel();
    ~FakeModel() override;
    bool TryStart(ModelInput input) override;
    std::optional<ModelReply> Poll() override;
    void Cancel(DecisionId id) override;
    void Close() override;
    bool EnqueuePlan(FakePlan plan);
    bool Release(DecisionId id);
    FakeStatistics Statistics() const;

  private:
    struct Job { ModelInput Input; FakePlan Plan; Clock::time_point Due; };
    mutable std::mutex Mutex;
    std::condition_variable Changed;
    std::vector<Job> Jobs;
    std::deque<FakePlan> Plans;
    std::deque<ModelReply> Replies;
    uint64_t Completed = 0;
    bool Closed = false;
    std::thread Worker;
    void Run();
    static ModelReply Decide(const Job &job);
};
} // namespace pixel::will
