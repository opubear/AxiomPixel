#include "Runtime/Gameplay/FormulaBridge/FormulaBridge.h"
#include "Runtime/Gameplay/FormulaBridge/CheckpointCodec.h"
#include "Runtime/Gameplay/FormulaBridge/ProcessHost.h"
#include "../Support/TestAccess.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
namespace process = formula::process;
void Check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
process::ProgramArtifact Program(std::string_view source) {
    auto result = process::Compile("world-composition", source);
    if (const auto *error = std::get_if<process::Diagnostic>(&result)) throw std::runtime_error(error->Message);
    return std::get<process::ProgramArtifact>(std::move(result));
}
WorldPosition At(double x, double y = 10.5) { return WorldPosition::FromAbsolute(x, y); }
ProcessCarrier Carrier(ProcessCarrierKind kind = ProcessCarrierKind::NaturalSource) {
    ProcessCarrier carrier;
    carrier.Kind = kind; carrier.Subject = 87; carrier.Origin = At(10.5);
    carrier.Scope = {0, 0, 128, 64};
    return carrier;
}
void SharedBodyAndRefusals() {
    const auto program = Program("formula 2; fn mine(radius: cells) -> action = dig(radius); run mine(cells(0));");
    std::uint64_t work = 0;
    for (const auto kind : {ProcessCarrierKind::NaturalSource, ProcessCarrierKind::Player, ProcessCarrierKind::Device}) {
        World world(64, 64); Actors actors;
        const auto actor = actors.Spawn(ActorKind::Player, At(10.5));
        FormulaBridge bridge; bridge.BindActors(actors); bridge.ApplyProcess(program);
        auto carrier = Carrier(kind);
        if (kind == ProcessCarrierKind::Player) {
            carrier.Binding = ProcessBindingKind::Actor; carrier.AttachedActor = actors.Require(*actor).Id;
            carrier.Origin = At(30.5); // Cache is not authoritative for an attachment.
        }
        world.SetCell(10, 10, {CellTypeId::GoldOre});
        Check(bridge.CastCarrier(world, carrier), "carrier rejected shared body");
        Check(world.GetCell(10, 10).Type == CellTypeId::Air, "shared body did not use the game mining API");
        const auto &receipt = bridge.ProcessReceipts().back();
        Check(receipt.Carrier == kind && receipt.Subject == 87 && receipt.Program == program.Identity()
            && receipt.Origin.Cell() == CellCoord{10, 10} && receipt.Source.Offset < program.Source().size(),
            "receipt lost independent source or exact call information");
        if (work) Check(work == bridge.ProcessStats().Work, "carrier changed composition semantics");
        work = bridge.ProcessStats().Work;
        bridge.Step(world);
        Check(bridge.Stats().Roots == 0, "completed action leaked an instance");

        bridge.BeginFrame(); world.SetCell(10, 10, {CellTypeId::GoldOre}); carrier.Credits = 0;
        Check(bridge.CastCarrier(world, carrier), "resource refusal should be an API result");
        Check(bridge.ProcessReceipts().back().Refusal == ProcessRefusal::Resources
            && world.GetCell(10, 10).Type == CellTypeId::GoldOre, "resource denial changed the world");
        bridge.Step(world);
        Check(bridge.Stats().Roots == 0, "resource denial did not propagate");
        carrier.Grants = process::Mask(process::Capability::Bolt);
        Check(!bridge.CastCarrier(world, carrier), "function encapsulation bypassed grants");
    }
}

void ActorFollowingAndFrozenState() {
    World world(64, 64); Actors actors;
    const auto actor = actors.Spawn(ActorKind::Player, At(10.5));
    FormulaBridge bridge; bridge.BindActors(actors);
    bridge.ApplyProcess(Program("formula 2; run repeat(2, ticks(2), dig(cells(0)));"));
    auto carrier = Carrier(ProcessCarrierKind::FixedFixture);
    carrier.Binding = ProcessBindingKind::Actor; carrier.AttachedActor = actors.Require(*actor).Id; carrier.LocalOffset = {1, 0};
    world.SetCell(11, 10, {CellTypeId::Stone}); world.SetCell(21, 10, {CellTypeId::CopperOre});
    Check(bridge.CastCarrier(world, carrier), "actor attachment failed");
    bridge.Step(world);
    const auto before = bridge.InspectProcesses().front();
    actors.SetPlayerPosition(At(70.5)); bridge.SynchronizeActivity(world);
    Check(bridge.InspectProcesses().front().Suspended, "out-of-region attachment did not freeze");
    for (int i = 0; i < 6; ++i) bridge.Step(world);
    Check(bridge.InspectProcesses().front().State.ActiveSteps == before.State.ActiveSteps, "frozen attachment advanced");
    const auto saved = bridge.CaptureState();
    bridge.Clear(world); bridge.RestoreState(saved);
    Check(bridge.CaptureState() == saved, "binding/continuation checkpoint did not round trip");
    actors.SetPlayerPosition(At(20.5)); bridge.SynchronizeActivity(world);
    Check(!bridge.InspectProcesses().front().Suspended, "moving attachment did not recover from stale cached origin");
    bridge.Step(world); bridge.Step(world);
    Check(world.GetCell(11, 10).Type == CellTypeId::Air && world.GetCell(21, 10).Type == CellTypeId::Air
        && bridge.ProcessReceipts().size() == 2, "resumed descriptor repeated or failed to follow its actor");
    Check(bridge.ProcessReceipts().back().Subject == 87, "attachment overwrote provenance");
    bridge.Step(world);
    Check(bridge.Stats().Roots == 0, "repeat did not finish");
}

void OwnedEffectsAndLostBinding() {
    World world(64, 64); Actors actors;
    auto handle = actors.Spawn(ActorKind::Player, At(10.5));
    FormulaBridge bridge; bridge.BindActors(actors);
    bridge.ApplyProcess(Program("formula 2; run field(cells(2), acceleration(120), acceleration(0), ticks(8));"));
    auto carrier = Carrier(); carrier.Binding = ProcessBindingKind::Actor;
    carrier.AttachedActor = actors.Require(*handle).Id; carrier.AngleDegrees = 90;
    Check(bridge.CastCarrier(world, carrier), "attached field failed");
    const auto fieldState = world.GetEnvironment().GetFieldSources().CaptureState();
    Check(fieldState.Sources.size() == 1, "field not installed");
    const auto &field = fieldState.Sources[0];
    Check(std::abs(field.Spec.Acceleration.X) < .001 && std::abs(field.Spec.Acceleration.Y - 120) < .001,
        "formula direction did not rotate field parameters");
    actors.SetPlayerPosition(At(20.5)); bridge.Step(world);
    Check(world.GetEnvironment().GetFieldSources().Sources()[0].Spec.Center.Cell() == CellCoord{10, 10},
        "installed world field followed actor implicitly");
    const auto saved = bridge.CaptureState();
    bridge.Clear(world); bridge.RestoreState(saved); world.RestoreFieldSources(fieldState);
    bridge.Step(world);
    Check(bridge.ProcessReceipts().size() == 1 && world.GetEnvironment().GetFieldSources().Sources().size() == 1,
        "restore repeated installation/payment");
    actors.Remove(*handle); bridge.SynchronizeActivity(world);
    Check(bridge.Stats().Roots == 0 && world.GetEnvironment().GetFieldSources().Sources().empty()
        && bridge.ProcessDiagnostics().back().Message.starts_with("ActorBindingLost"), "lost actor did not cancel owned effects");
    Check(!bridge.CastCarrier(world, carrier) && bridge.LastError() == "ActorBindingLost", "stale actor admitted");
    bool refused = false;
    try { bridge.RestoreState(saved); } catch (const std::invalid_argument &) { refused = true; }
    Check(refused && bridge.Stats().Roots == 0, "restore silently rebound a missing actor");

    handle = actors.Spawn(ActorKind::Player, At(10.5)); carrier.AttachedActor = actors.Require(*handle).Id;
    bridge.ApplyProcess(Program("formula 2; run delay(ticks(9), dig(cells(1)));"));
    Check(bridge.CastCarrier(world, carrier), "new actor attachment failed");
    actors.SetPlayerPosition(At(140.5)); bridge.SynchronizeActivity(world);
    Check(bridge.Stats().Roots == 0 && bridge.ProcessDiagnostics().back().Message.starts_with("ActorBindingScope"),
        "attachment expanded its authority scope");
}

void LegacyHostCheckpoint() {
    // Version-1 wire fixture: one committed dig, a sleeping continuation and
    // its undelivered receipt. Deliberately omit all v2 binding/trace fields.
    const auto program = Program("process 1; emit dig(cells(0)); wait ticks(2);");
    auto instance = std::get<std::unique_ptr<process::Instance>>(process::Instance::Start(program, 1,
        process::AllCapabilities, {}, {10, 10, .5, .5}));
    const auto output = instance->Step({1, 6});
    checkpoint::Writer wire;
    wire.String("Pixel.ProcessHost"); wire.Numbers(1, 1, 1);
    checkpoint::Write(wire, program); checkpoint::Write(wire, instance->ExportState());
    const auto carrier = Carrier();
    wire.Enum(carrier.Kind); wire.Numbers(carrier.Subject); checkpoint::Write(wire, carrier.Origin);
    wire.Numbers(carrier.Scope.X, carrier.Scope.Y, carrier.Scope.Width, carrier.Scope.Height, carrier.AngleDegrees,
        carrier.MaximumRadius, carrier.ActivityRadius, 5, carrier.Grants,
        carrier.Limits.Work, carrier.Limits.Queries, carrier.Limits.Outputs);
    wire.Numbers(1, 2, 2, false, false, 1);
    checkpoint::Write(wire, process::HostEvent{1, 1, 1, process::Outcome::Applied, output.Requests[0].Id});
    wire.Numbers(0, 1); // no bolts, one historical receipt
    wire.Numbers(1, 1, 1); wire.Enum(carrier.Kind); wire.Numbers(carrier.Subject);
    wire.Enum(process::Outcome::Applied); wire.Enum(ProcessRefusal::None); wire.Numbers(1, 0); // changed, no diagnostics
    ProcessHost host; host.RestoreState(wire.Finish());
    Check(host.Inspect()[0].Configuration.Binding == ProcessBindingKind::World, "old carrier did not migrate to world positioning");
    World world(64, 64); world.SetCell(10, 10, {CellTypeId::GoldOre});
    host.Prepare(world); host.Commit(world); host.Prepare(world); host.Commit(world);
    Check(host.Stats().Instances == 0 && world.GetCell(10, 10).Type == CellTypeId::GoldOre,
        "legacy continuation replayed committed mining");
}

void SessionPauseAndEditing() {
    GameSession session(false);
    auto program = Program("formula 2; run delay(ticks(3), bolt(1, speed(0), ticks(2)));");
    Check(bool(session.ApplyProcess(NativeCaller::Driver, program)), "new formula not accepted by session editor port");
    auto carrier = Carrier(ProcessCarrierKind::FixedFixture);
    carrier.Binding = ProcessBindingKind::Actor; carrier.AttachedActor = session.GetActors().PlayerSnapshot()->Id;
    carrier.LocalOffset = {3, 1}; carrier.Scope = session.GetWorld().GetSimulationBounds();
    Check(session.StartProcessFixture(NativeCaller::Driver, carrier).Succeeded(), "session actor fixture refused");
    const auto before = session.GetFormulaRuntime().InspectProcesses().front();
    session.Control(NativeCaller::Driver, api::SetPaused{true});
    for (int i = 0; i < 8; ++i) session.Tick(1.f / 60, {});
    Check(session.GetFormulaRuntime().InspectProcesses().front().State.ActiveSteps == before.State.ActiveSteps,
        "paused session advanced attached composition");
    Check(bool(session.ApplyProcess(NativeCaller::Driver, Program("formula 2; run nothing();"))) == false,
        "paused session unexpectedly allowed publication");
    session.Control(NativeCaller::Driver, api::SetPaused{false});
    Check(bool(session.ApplyProcess(NativeCaller::Driver, Program("formula 2; run nothing();"))), "unpaused publication failed");
    Check(session.GetFormulaRuntime().InspectProcesses().front().Program == before.Program,
        "editing replaced existing composition");
}
} // namespace
int main() {
    try {
        SharedBodyAndRefusals(); ActorFollowingAndFrozenState(); OwnedEffectsAndLostBinding(); LegacyHostCheckpoint(); SessionPauseAndEditing();
        std::cout << "formula composition/world binding tests passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
