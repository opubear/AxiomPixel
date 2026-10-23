#include "Gameplay/GameSession.h"
#include "WorldWill/FakeModel.h"
#include "WorldWill/WorldWill.h"
#include <iostream>
#include <thread>

using namespace pixel;
using namespace std::chrono_literals;
int main() {
    GameSession session;
    auto compiled = formula::process::Compile("offline-world-will",
        "process 1; emit field(cells(4), acceleration(100), acceleration(0), ticks(1200)); "
        "await result within ticks(1); wait ticks(1000);");
    if (!session.ApplyProcess(NativeCaller::Driver, std::get<formula::process::ProgramArtifact>(compiled))) return 1;
    session.Control(NativeCaller::Driver, api::SetPaused{false});
    will::FakeModel model;
    will::WorldWill decisions(session, model);
    model.EnqueuePlan({0ms, true});
    const auto request = decisions.Request({{{80, 80}, 40, 40}, {100, 100}, {}});
    if (request.Status != will::RequestStatus::Accepted) return 2;
    for (int i = 0; i < 4; ++i) {
        session.BeginFrame();
        decisions.Service();
        session.Tick(1.f / 60.f, {});
    }
    std::cout << "model pending=" << model.Statistics().Pending
              << "; simulation tick=" << session.CurrentCommit().Tick << '\n';
    model.Release(request.Id);
    const auto deadline = will::Clock::now() + 3s;
    while (decisions.Results().empty() && will::Clock::now() < deadline) {
        decisions.Service();
        std::this_thread::sleep_for(1ms);
    }
    if (decisions.Results().empty()) return 3;
    const auto &result = decisions.Results().back();
    std::cout << "decision=" << result.Id << "; state=" << will::DecisionStateName(result.State);
    if (result.Action) {
        std::cout << "; action sequence=" << result.Action->Key.Sequence
                  << "; facts=" << result.Action->Facts.Count;
    }
    std::cout << "; field acceleration x=" << session.GetWorld().GetForce(100, 100).X
              << "; free influence slots=" << session.GetAuthority().Resources().Available << '\n';
    return result.State == will::DecisionState::Applied ? 0 : 4;
}
