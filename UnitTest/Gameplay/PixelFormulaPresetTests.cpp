#include "Dev/Debug/Formula/FormulaPresets.h"
#include "ProcessFakeHost.h"
#include "../Support/TestAccess.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
namespace process = formula::process;

void Check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

process::testing::FakeHost Host(size_t preset) {
    return process::testing::FakeHost(std::get<std::unique_ptr<process::Instance>>(
        process::Instance::Start(FormulaPresetProgram(preset), 1)));
}

void Advance(process::testing::FakeHost &host, int steps, bool hit = false) {
    for (int tick = 0; tick < steps && host.Process().State().Status != process::Phase::Ended; ++tick) {
        if (hit && host.OwnedCount()) host.Hit({17, 23, .25, .75});
        Check(!host.Advance().Fault, "preset exceeded runtime limits");
    }
}

void DescriptorBehavior() {
    for (size_t index = 0; index < FormulaPresets().size(); ++index) {
        const auto &program = FormulaPresetProgram(index);
        Check(program.Identity().SemanticsVersion == 2 && program.Source() == FormulaPresets()[index].Source,
            "preset did not retain its formula/2 source");
    }
    auto volley = Host(0);
    Advance(volley, 150, true);
    Check(volley.Trace().size() == 6 && volley.Process().State().Status == process::Phase::Ended,
        "mining volley did not finish three shots and three impacts");
    for (size_t shot = 0; shot < 3; ++shot) {
        const auto &bolt = volley.Trace()[shot * 2];
        const auto &dig = volley.Trace()[shot * 2 + 1];
        Check(std::holds_alternative<process::Bolt>(bolt.Request.Description)
            && std::get<process::Dig>(dig.Request.Description).Radius == 2
            && bolt.Request.Origin == process::Position{}
            && dig.Request.Origin == process::Position{17, 23, .25, .75}, "volley lost hit/parent context");
        if (shot) Check(bolt.At >= volley.Trace()[shot * 2 - 1].At + 20, "volley skipped its interval");
    }

    auto delayed = Host(1);
    Advance(delayed, 120, true);
    Check(delayed.Trace().size() == 3 && delayed.Trace()[1].At >= 30
        && delayed.Trace()[2].At >= delayed.Trace()[1].At + 45
        && std::get<process::Dig>(delayed.Trace()[1].Request.Description).Radius == 1
        && std::get<process::Dig>(delayed.Trace()[2].Request.Description).Radius == 4
        && delayed.Trace()[1].Request.Origin == delayed.Trace()[2].Request.Origin,
        "delayed excavation lost its two stages, delays or fixed hit point");

    auto force = Host(2);
    Advance(force, 410);
    Check(force.Trace().size() == 4 && force.OwnedCount() == 0, "force cycles leaked or skipped a field");
    for (size_t i = 0; i < 4; ++i) {
        const auto &field = std::get<process::FieldSource>(force.Trace()[i].Request.Description);
        Check(field.AccelerationX == (i % 2 ? -3600 : 3600) && field.Lifetime == 90,
            "force preset did not alternate direction and duration");
        if (i) Check(force.Trace()[i].At >= force.Trace()[i - 1].At + 90, "force stages overlap");
    }

    auto beacon = Host(3);
    Advance(beacon, 400);
    Check(beacon.Trace().size() == 6 && beacon.OwnedCount() == 0, "beacon did not complete six flashes");
    for (const auto &entry : beacon.Trace())
        Check(std::get<process::Bolt>(entry.Request.Description).VelocityX == 0, "beacon should stay where emitted");

    auto launcher = Host(4);
    Advance(launcher, 5);
    Check(launcher.Trace().empty(), "launcher fired before its wake");
    launcher.Wake();
    Advance(launcher, 120, true);
    Check(launcher.Trace().size() == 2 && launcher.OwnedCount() == 0, "wake did not release the mining shot");
    auto timeout = Host(4);
    Advance(timeout, 602);
    Check(timeout.Trace().empty() && timeout.Process().State().Status == process::Phase::Ended,
        "launcher timeout fired or retained a waiting instance");
}

void SessionControlsAndBinding() {
    GameSession session;
    auto &runtime = testing::SessionAccess::Runtime(session);
    auto &world = testing::SessionAccess::World(session);
    const auto initial = session.AppliedProgram();
    session.Control(NativeCaller::Debug, api::SetPaused{true});
    Check(RunFormulaPreset(session, 0, 35).Rejection == api::Reason::GamePaused
        && session.AppliedProgram() == initial && runtime.InspectProcesses().empty(), "paused preset mutated the session");
    session.Control(NativeCaller::Debug, api::SetPaused{false});
    for (size_t index = 0; index < FormulaPresets().size(); ++index) {
        Check(RunFormulaPreset(session, index, FormulaPresets()[index].AngleDegrees).Succeeded(), "preset failed to start in the game");
        Check(runtime.ActiveProcess().Identity() == FormulaPresetProgram(index).Identity(), "run did not publish the selected program");
        if (index == 0) {
            for (int step = 0; step < 150; ++step) runtime.Step(world);
            const auto &receipts = runtime.ProcessReceipts();
            Check(std::count_if(receipts.begin(), receipts.end(), [](const auto &receipt) {
                return receipt.Subject == FormulaPresetSubject(0) && receipt.ChangedCells > 0;
            }) == 3, "default mining volley did not hit and mine real terrain three times");
        }
        Check(StopFormulaPreset(session, index).Succeeded(), "preset stop was refused");
        Check(runtime.Stats().Roots == 0 && runtime.Projectiles().empty() && world.GetEnvironment().GetFieldSources().Sources().empty(),
            "stopping a preset leaked owned work");
    }

    Check(RunFormulaPreset(session, 3, 0).Succeeded(), "beacon start failed");
    const auto oldId = runtime.InspectProcesses().front().State.Id;
    Check(RunFormulaPreset(session, 4, 35).Succeeded() && RunFormulaPreset(session, 3, 0).Succeeded(), "preset coexistence failed");
    const auto instances = runtime.InspectProcesses();
    Check(instances.size() == 2 && std::none_of(instances.begin(), instances.end(), [oldId](const auto &value) {
        return value.State.Id == oldId;
    }), "restart duplicated this preset or stopped another preset");

    const auto first = runtime.Projectiles().front().Position;
    auto moved = session.GetPlayer()->GetWorldPosition();
    moved.Local[0] += 5;
    testing::SessionAccess::Actors(session).SetPlayerPosition(moved);
    runtime.SynchronizeActivity(world);
    Check(runtime.Projectiles().front().Position.Cell() == first.Cell(), "actor movement moved an emitted beacon");
    for (int step = 0; step < 62; ++step) runtime.Step(world);
    Check(runtime.Projectiles().size() == 1 && runtime.Projectiles().front().Position.Cell().X == moved.Cell().X + 1,
        "next beacon did not follow the actor");
    Check(WakeFormulaPreset(session, 4).Succeeded(), "wake control was refused");
    runtime.Step(world);
    Check(runtime.Projectiles().size() == 2, "wake control did not launch the waiting shot");
    StopFormulaPreset(session, 4);
    Check(runtime.InspectProcesses().size() == 1, "stopping launcher also stopped the beacon");
    StopFormulaPreset(session, 3);
    Check(runtime.Stats().Roots == 0 && runtime.Projectiles().empty(), "preset cleanup retained effects");
}
} // namespace

int main() {
    try {
        DescriptorBehavior();
        SessionControlsAndBinding();
        std::cout << "Formula presets passed: five descriptors, timing, effects, wake/timeout, restart isolation and Actor following\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
