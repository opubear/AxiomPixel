#include "FakeModel.h"
#include <algorithm>
#include <stdexcept>

namespace pixel::llm {
FakeModel::FakeModel() : Worker([this] { Run(); }) {}

FakeModel::~FakeModel() { Close(); }

bool FakeModel::TryStart(ModelInput input) {
    if (!input.Id || input.RetainedBytes() > ModelLimits::InputBytes)
        throw std::invalid_argument("fake model requires a bounded, identified input");

    std::lock_guard lock(Mutex);
    // Completed replies also consume capacity until the host drains them.
    if (Closed || Jobs.size() + Replies.size() >= ModelLimits::InFlight) return false;
    const auto plan = Plans.empty() ? FakePlan{} : Plans.front();
    if (!Plans.empty()) Plans.pop_front();
    Jobs.push_back({std::move(input), plan, Clock::now() + plan.Delay});
    Changed.notify_one();
    return true;
}

std::optional<ModelReply> FakeModel::Poll() {
    std::lock_guard lock(Mutex);
    if (Replies.empty()) return {};
    auto reply = std::move(Replies.front());
    Replies.pop_front();
    return reply;
}

void FakeModel::Cancel(DecisionId id) {
    std::lock_guard lock(Mutex);
    std::erase_if(Jobs, [&](const auto &job) { return job.Input.Id == id; });
    std::erase_if(Replies, [&](const auto &reply) { return reply.Id == id; });
    Changed.notify_one();
}

void FakeModel::Close() {
    {
        std::lock_guard lock(Mutex);
        Closed = true;
        Jobs.clear();
        Replies.clear();
        Plans.clear();
        Changed.notify_one();
    }
    if (Worker.joinable()) Worker.join();
}

bool FakeModel::EnqueuePlan(FakePlan plan) {
    if (plan.Delay.count() < 0 || plan.Delay > std::chrono::seconds(60))
        throw std::invalid_argument("fake model delay outside [0ms,60s]");
    std::lock_guard lock(Mutex);
    if (Closed || Plans.size() == 8) return false;
    Plans.push_back(plan);
    return true;
}

bool FakeModel::Release(DecisionId id) {
    std::lock_guard lock(Mutex);
    const auto found = std::find_if(Jobs.begin(), Jobs.end(), [&](const auto &job) { return job.Input.Id == id; });
    if (found == Jobs.end()) return false;
    found->Plan.HoldUntilReleased = false;
    Changed.notify_one();
    return true;
}

FakeStatistics FakeModel::Statistics() const {
    std::lock_guard lock(Mutex);
    return {Jobs.size(), Replies.size(), Plans.size(), Completed, Closed};
}

ModelReply FakeModel::Decide(const Job &job) {
    ModelReply reply;
    reply.Id = job.Input.Id;
    if (job.Plan.Unavailable) {
        reply.Status = ModelStatus::Unavailable;
        reply.Explanation = "offline fake model unavailable";
        return reply;
    }
    const auto &input = job.Input;
    if (input.Intent.Device) {
        const auto found = std::find_if(input.Devices.begin(), input.Devices.end(),
            [&](const auto &device) { return device.Ref.Id == *input.Intent.Device; });
        if (found != input.Devices.end()) reply.Choice = {ProposalKind::AtDevice, {}, found->Ref.Id};
    } else {
        const auto found = std::find_if(input.Materials.begin(), input.Materials.end(),
            [&](const auto &cell) { return cell.Location == input.Intent.Target && cell.MaterialId.has_value(); });
        if (found != input.Materials.end()) reply.Choice = {ProposalKind::AtCell, found->Location, 0};
    }
    reply.Explanation = reply.Choice.Kind == ProposalKind::None
        ? "requested target is absent from the delivered observation"
        : "propose the offered intervention at the observed target";
    return reply;
}

void FakeModel::Run() {
    std::unique_lock lock(Mutex);
    while (!Closed) {
        auto due = Clock::time_point::max();
        auto ready = Jobs.end();
        const auto now = Clock::now();
        for (auto it = Jobs.begin(); it != Jobs.end(); ++it) {
            if (it->Plan.HoldUntilReleased) continue;
            if (it->Due <= now) { ready = it; break; }
            due = std::min(due, it->Due);
        }
        if (ready == Jobs.end()) {
            if (due == Clock::time_point::max()) Changed.wait(lock);
            else Changed.wait_until(lock, due);
            continue;
        }
        // Only a bounded in-memory lookup happens under this lock. Delays and
        // holds use the condition variable and never retain the mutex.
        auto reply = Decide(*ready);
        if (ready->Plan.DuplicateReply) Replies.push_back(reply);
        Replies.push_back(std::move(reply));
        Jobs.erase(ready);
        ++Completed;
    }
}
} // namespace pixel::llm
