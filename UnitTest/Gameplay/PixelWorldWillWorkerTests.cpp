#include "Gameplay/GameSession.h"
#include "WorldWill/FakeModel.h"
#include "WorldWill/WorldWill.h"
#include <iostream>
#include <thread>

using namespace pixel;
using namespace std::chrono_literals;
namespace {
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

void Configure(GameSession &session) {
    auto program = formula::process::Compile("worker-test",
        "process 1; emit field(cells(4), acceleration(100), acceleration(0), ticks(1200)); "
        "await result within ticks(1); wait ticks(1000);");
    Check(bool(session.ApplyProcess(NativeCaller::Driver, std::get<formula::process::ProgramArtifact>(program))), "compile fixture");
    Check(session.Control(NativeCaller::Driver, api::SetPaused{false}).Succeeded(), "unpause fixture");
}

void WaitForReply(will::FakeModel &model) {
    const auto deadline = will::Clock::now() + 3s;
    while (!model.Statistics().Replies && will::Clock::now() < deadline) std::this_thread::sleep_for(1ms);
    Check(model.Statistics().Replies != 0, "worker failed to publish completion");
}

const will::Goal Region{{{80, 80}, 40, 40}, {100, 100}, {}};

void WorkerProgressAndOrder() {
    GameSession session;
    Configure(session);
    will::FakeModel model;
    will::WorldWill decisions(session, model);
    Check(model.EnqueuePlan({0ms, true, true}), "queue first held plan");
    Check(model.EnqueuePlan({0ms, true, true}), "queue second held plan");
    const auto older = decisions.Request(Region), newer = decisions.Request(Region);
    Check(older.Status == will::RequestStatus::Accepted && newer.Status == will::RequestStatus::Accepted, "start worker requests");
    Check(decisions.Request(Region).Status == will::RequestStatus::Capacity, "in-flight requests not bounded");
    const auto before = session.CurrentCommit();
    for (int i = 0; i < 4; ++i) {
        session.BeginFrame();
        decisions.Service();
        session.Tick(1.f / 60.f, {});
    }
    Check(session.CurrentCommit().Tick > before.Tick && model.Statistics().Pending == 2
        && model.Statistics().Completed == 0 && decisions.Results().empty(), "simulation waited on held model work");

    Check(model.Release(newer.Id), "release newer request");
    WaitForReply(model);
    decisions.Service();
    Check(decisions.Results().size() == 1 && decisions.Results().front().Id == newer.Id
        && decisions.Results().front().State == will::DecisionState::Applied, "newer request did not finish first");
    Check(session.GetWorld().GetForce(100, 100).X == 100 && session.GetAuthority().Resources().Available == 3,
        "worker proposal had no actual field/resource effect");
    Check(model.Release(older.Id), "release older request");
    WaitForReply(model);
    decisions.Service();
    Check(decisions.Results().size() == 2 && decisions.Results().back().Id == older.Id
        && decisions.Results().back().State == will::DecisionState::Rejected
        && decisions.Results().back().Action->Rejection == api::Reason::PreconditionFailed,
        "late request ignored current resource precondition");
    Check(session.GetAuthority().Resources().Available == 3 && decisions.Statistics().IgnoredReplies == 2,
        "duplicate worker replies executed more than once");
    decisions.Close();
    Check(model.Statistics().Closed && model.Statistics().Pending == 0 && model.Statistics().Replies == 0, "closed worker retained data");
}

void DelayUnavailableAndShutdown() {
    GameSession session;
    Configure(session);
    will::FakeModel model;
    will::WorldWill decisions(session, model);
    Check(model.EnqueuePlan({25ms, false, false, true}), "queue delayed unavailable plan");
    Check(decisions.Request(Region).Status == will::RequestStatus::Accepted, "start unavailable request");
    WaitForReply(model);
    decisions.Service();
    Check(decisions.Results().back().State == will::DecisionState::ModelUnavailable
        && session.GetAuthority().Resources().Available == 4, "unavailability mutated gameplay");

    Check(model.EnqueuePlan({60s, true}), "queue long held job");
    Check(decisions.Request(Region).Status == will::RequestStatus::Accepted, "start close fixture");
    const auto start = will::Clock::now();
    decisions.Close();
    Check(will::Clock::now() - start < 1s, "shutdown waited for model delay instead of cancellation");
    Check(decisions.Results().back().State == will::DecisionState::Closed && model.Statistics().Closed,
        "close did not retire outstanding work");
    Check(decisions.Request(Region).Status == will::RequestStatus::Closed, "closed controller restarted work");
}

void BoundedAdapter() {
    will::FakeModel model;
    for (int i = 0; i < 8; ++i) Check(model.EnqueuePlan({0ms, true}), "plan capacity too small");
    Check(!model.EnqueuePlan({}), "plan queue unbounded");
    will::ModelInput first, second, third;
    first.Id = 1; second.Id = 2; third.Id = 3;
    Check(model.TryStart(std::move(first)) && model.TryStart(std::move(second)) && !model.TryStart(std::move(third)),
        "worker input queue unbounded");
    model.Cancel(1);
    Check(!model.Release(1) && model.Statistics().Pending == 1, "cancel retained input");
    model.Close();
    Check(model.Statistics().Pending == 0 && model.Statistics().Plans == 0, "close retained queued work");
}
}

int main() {
    try {
        WorkerProgressAndOrder();
        DelayUnavailableAndShutdown();
        BoundedAdapter();
        std::cout << "WorldWill worker tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
