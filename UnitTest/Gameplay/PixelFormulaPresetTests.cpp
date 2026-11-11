#include "Dev/Debug/Formula/FormulaPresets.h"
#include "Runtime/Gameplay/Items/HandheldItem.h"
#include "Runtime/Gameplay/FormulaBridge/DefaultProcess.h"
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

void SessionEquipmentAndActivation() {
    GameSession session;
    auto &runtime = testing::SessionAccess::Runtime(session);
    auto &world = testing::SessionAccess::World(session);
    auto &inventory = testing::SessionAccess::Inventory(session);
    items::ItemRef stick;
    Check(inventory.CreateStick(inventory.Reference(session.PlayerInventory()), DefaultPlayerProcess(), stick)
        == items::Result::Applied, "stick fixture failed");
    const auto equipped = [&]() { return items::ItemProgram(*inventory.FindInstance(session.PlayerInventory(), stick)); };
    const auto equip = [&](size_t index) { return session.ApplyHeldProcess(NativeCaller::Debug, stick, FormulaPresetProgram(index)); };
    for (const auto &actor : session.GetActors().CaptureState().Records)
        Check(!actor.Formula, "actor was automatically equipped");
    const auto initial = equipped();
    session.Control(NativeCaller::Debug, api::SetPaused{true});
    Check(equip(0).Rejection == api::Reason::GamePaused
        && equipped() == initial && runtime.InspectProcesses().empty(), "paused equip mutated the session");
    session.Control(NativeCaller::Debug, api::SetPaused{false});
    const auto activate = [&] {
        Check(session.Execute(NativeCaller::Input,
            api::UseHeldItem{stick, {{530, 508}, {.25, .5}}}).Applied(), "equipped cast failed");
    };
    for (size_t index = 0; index < FormulaPresets().size(); ++index) {
        Check(bool(equip(index)), "preset failed to equip");
        Check(inventory.FindInstance(session.PlayerInventory(), stick)->Formula->Identity() == FormulaPresetProgram(index).Identity(), "equip lost the program identity");
        for (int step = 0; step < 5; ++step) runtime.Step(world);
        Check(runtime.Stats().Roots == 0 && runtime.Projectiles().empty()
            && world.GetEnvironment().GetFieldSources().Sources().empty(), "equipping automatically ran the preset");
        activate();
        const auto instances = runtime.InspectProcesses();
        Check(instances.size() == 1 && instances[0].Carrier == ProcessCarrierKind::Player
            && instances[0].Subject == session.GetActors().PlayerSnapshot()->Id.Value,
            "activation did not belong to the equipped actor");
        if (FormulaPresets()[index].WaitsForWake) {
            Check(runtime.Projectiles().empty(), "launcher fired before wake");
            Check(WakePlayerFormulas(session).Succeeded(), "wake failed");
            runtime.Step(world);
            Check(runtime.Projectiles().size() == 1, "wake did not fire the equipped launcher");
        }
        Check(StopPlayerFormulas(session).Succeeded(), "stop failed");
        Check(runtime.Stats().Roots == 0 && runtime.Projectiles().empty()
            && world.GetEnvironment().GetFieldSources().Sources().empty(), "stop leaked owned effects");
    }
    Check(bool(equip(3)), "beacon equip failed");
    activate();
    const auto old = runtime.InspectProcesses().front();
    Check(bool(equip(4)), "launcher equip failed");
    Check(runtime.InspectProcesses().size() == 1
        && runtime.InspectProcesses().front().State.Id == old.State.Id
        && runtime.InspectProcesses().front().Version == old.Version, "equipping altered an existing cast");
    activate();
    Check(runtime.InspectProcesses().size() == 2, "explicit activation replaced an old cast");
    StopPlayerFormulas(session);
    const auto oldPlayer = session.GetActors().PlayerSnapshot()->Id;
    Check(session.Control(NativeCaller::Debug, api::ResetWorld{}).Succeeded(), "reset failed");
    Check(session.GetActors().PlayerSnapshot()->Id != oldPlayer && !inventory.FindInstance(session.PlayerInventory(), stick)
        && runtime.InspectProcesses().empty(), "reset retained a stick or replayed a cast");
}

} // namespace

int main() {
    try {
        DescriptorBehavior();
        SessionEquipmentAndActivation();
        std::cout << "Formula presets passed: five descriptors, timing, effects, wake/timeout, player equipment, explicit activation, version retention and reset\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
