#include "Gameplay/FormulaBridge/FormulaBridge.h"
#include "Formula/Process/Editing.h"
#include "Gameplay/FormulaBridge/ProcessExample.h"
#include "Gameplay/GameSession.h"
#include "ProcessFakeHost.h"
#include "../Support/TestAccess.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace pixel;
namespace process = formula::process;
void Check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
process::ProgramArtifact Program(std::string_view source) {
    auto result = process::Compile("bridge-test", source);
    if (const auto *error = std::get_if<process::Diagnostic>(&result)) throw std::runtime_error(error->Message);
    return std::get<process::ProgramArtifact>(result);
}
WorldPosition At(double x, double y) { return WorldPosition::FromAbsolute(x, y); }
ProcessCarrier Carrier(ProcessCarrierKind kind = ProcessCarrierKind::Player) {
    ProcessCarrier result;
    result.Kind = kind;
    result.Origin = At(10.5, 10.5);
    result.Scope = {0, 0, 64, 64};
    return result;
}
void ThreeCarriers() {
    const auto program = Program("process 1; emit dig(cells(0)); await result within ticks(2); wait ticks(2);");
    uint64_t work = 0;
    for (const auto kind : {ProcessCarrierKind::Player, ProcessCarrierKind::NaturalSource, ProcessCarrierKind::FixedFixture}) {
        World world(64, 64);
        world.SetCell(10, 10, {CellTypeId::GoldOre});
        FormulaBridge bridge;
        bridge.ApplyProcess(program);
        Check(bridge.CastCarrier(world, Carrier(kind)), "carrier admission failed");
        Check(world.GetCell(10, 10).Type == CellTypeId::Air, "carrier did not use common mining rule");
        Check(bridge.LastEffects().size() == 1 && bridge.LastEffects()[0].ResourceId == 2, "missing process fact identity");
        const auto &receipt = bridge.ProcessReceipts().back();
        Check(receipt.Carrier == kind && receipt.Subject == 1 && receipt.ChangedCells == 1
            && receipt.Outcome == process::Outcome::Applied, "carrier provenance or result lost");
        if (work) Check(bridge.ProcessStats().Work == work, "carrier kind changed language budget");
        work = bridge.ProcessStats().Work;
        for (int i = 0; i < 4; ++i) bridge.Step(world);
        Check(bridge.Stats().Roots == 0 && bridge.ProcessStats().PendingEvents == 0, "normal termination leaked work");
    }
    auto instance = std::get<std::unique_ptr<process::Instance>>(process::Instance::Start(program, 1));
    process::testing::FakeHost fake(std::move(instance));
    fake.Advance();
    Check(fake.Trace().size() == 1 && fake.Trace()[0].Result == process::Outcome::Applied, "fake host semantic disagreement");
    std::cout << "three carriers: same dig fact, same " << work << " language work; explicit provenance\n";
}
void CollisionOrderAndExpiry() {
    World world(64, 64);
    world.SetCell(15, 10, {CellTypeId::Stone});
    FormulaBridge bridge;
    // A legacy delayed dig removes the wall in the same boundary as process sweeps.
    bridge.Apply(formula::Formula::Dig(6).After(1));
    bridge.Cast(world, At(10.5, 10.5), 0);
    bridge.ApplyProcess(Program("process 1; emit bolt(10, speed(600), ticks(5)); await hit within ticks(10); if outcome() == hit { emit dig(cells(1)); }"));
    Check(bridge.CastCarrier(world, Carrier()), "process bolt refused");
    Check(bridge.Projectiles().size() == 1 && bridge.Projectiles()[0].Position.Cell().X == 10, "birth moved early");
    bridge.Step(world);
    Check(bridge.Projectiles().empty() && world.GetCell(15, 10).Type == CellTypeId::Air, "sweep observed same-batch dig");
    Check(bridge.ProcessReceipts().size() == 1, "hit resumed in collision boundary");
    world.SetCell(15, 10, {CellTypeId::GoldOre});
    bridge.Step(world);
    Check(world.GetCell(15, 10).Type == CellTypeId::Air && bridge.ProcessReceipts().size() == 2,
        "hit did not resume next boundary at exact contact position");
    Check(bridge.Stats().Roots == 0, "EOF cleanup failed");

    bridge.BeginFrame();
    world.SetCell(15, 10, {CellTypeId::Stone});
    bridge.ApplyProcess(Program("process 1; emit bolt(10, speed(600), ticks(1)); await hit within ticks(3); if outcome() == expired { emit dig(cells(0)); }"));
    world.SetCell(10, 10, {});
    bridge.CastCarrier(world, Carrier());
    bridge.Step(world);
    Check(bridge.Projectiles().empty() && world.GetCell(15, 10).Type == CellTypeId::Stone, "expiry lost to final segment hit");
    world.SetCell(10, 10, {CellTypeId::Stone});
    bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::Air, "expired event did not return to original origin");

    bridge.BeginFrame();
    bridge.ApplyProcess(Program("process 1; emit bolt(10, speed(0), ticks(10)); await result within ticks(2); await hit within ticks(2); if outcome() == hit { emit dig(cells(0)); }"));
    world.SetCell(10, 10, {CellTypeId::Stone});
    bridge.CastCarrier(world, Carrier());
    Check(bridge.Projectiles().empty(), "birth overlap retained ball");
    bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::Stone, "two awaits merged into one slice");
    bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::Air, "early hit lost while awaiting admission");
}
void RefusalsAndResources() {
    World world(64, 64);
    FormulaBridge bridge;
    bridge.ApplyProcess(Program("process 1; emit bolt(10, speed(60), ticks(10)); wait ticks(1); emit bolt(10, speed(60), ticks(10)); await result within ticks(2); wait ticks(5);"));
    auto carrier = Carrier(ProcessCarrierKind::FixedFixture);
    carrier.Credits = 1;
    bridge.CastCarrier(world, carrier);
    bridge.Step(world);
    Check(bridge.ProcessReceipts().back().Refusal == ProcessRefusal::Resources && bridge.Projectiles().size() == 1,
        "resource refusal changed prior accepted carrier");
    bridge.CancelCarrier(world, carrier.Kind, carrier.Subject);
    Check(bridge.Projectiles().empty() && bridge.ProcessStats().PendingEvents == 0, "revocation did not clean ownership");

    bridge.BeginFrame();
    bridge.ApplyProcess(Program("process 1; emit dig(cells(17)); await result within ticks(2);"));
    world.SetCell(10, 10, {CellTypeId::Stone});
    Check(bridge.CastCarrier(world, Carrier()), "dynamic geometry should be a host refusal");
    Check(bridge.ProcessReceipts().back().Refusal == ProcessRefusal::Geometry
        && world.GetCell(10, 10).Type == CellTypeId::Stone, "oversized effect escaped host contract");
    bridge.Clear(world);
    carrier = Carrier();
    carrier.Grants = process::Mask(process::Capability::Bolt);
    Check(!bridge.CastCarrier(world, carrier), "missing capability admitted");
    bridge.ApplyProcess(Program("process 1; emit dig(cells(2)); wait ticks(5);"));
    carrier = Carrier(); carrier.Scope = {10, 10, 2, 2};
    Check(bridge.CastCarrier(world, carrier) && bridge.ProcessReceipts().back().Refusal == ProcessRefusal::Scope,
        "descendant disk escaped carrier scope");
    bridge.Clear(world);

    bridge.ApplyProcess(Program("process 1; emit bolt(10, speed(600), ticks(10)); await hit within ticks(4); if outcome() == cancelled { emit dig(cells(0)); }"));
    carrier = Carrier(); carrier.Scope = {9, 9, 3, 3};
    world.SetCell(10, 10, {});
    bridge.CastCarrier(world, carrier);
    bridge.Step(world);
    world.SetCell(10, 10, {CellTypeId::Stone});
    bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::Air, "scope exit did not deliver cancellation");

    bridge.Clear(world);
    bridge.BeginFrame();
    bridge.ApplyProcess(Program("process 1; emit bolt(10, speed(600), ticks(10)); await hit within ticks(4); if outcome() == hit { emit dig(cells(1)); }"));
    carrier = Carrier(); carrier.Scope = {0, 0, 16, 32};
    world.SetCell(10, 10, {});
    world.SetCell(12, 10, {CellTypeId::Stone});
    bridge.CastCarrier(world, carrier);
    bridge.Step(world);
    bridge.Step(world);
    Check(world.GetCell(12, 10).Type == CellTypeId::Air, "later scope exit hid earlier in-scope wall contact");
}
void FrozenQueriesVersionsAndFaults() {
    World world(64, 64);
    FormulaBridge bridge;
    const auto source = "process 1; emit bolt(10, speed(0), ticks(20)); if inventory() == 1 { emit bolt(20, speed(0), ticks(20)); } wait ticks(10);";
    bridge.ApplyProcess(Program(source));
    auto carrier = Carrier(); carrier.Credits = 1;
    bridge.CastCarrier(world, carrier);
    Check(bridge.ProcessReceipts().size() == 2 && bridge.ProcessReceipts().back().Refusal == ProcessRefusal::Resources,
        "query read effects from its own uncommitted slice");
    const auto oldVersion = bridge.Version();
    bridge.ApplyProcess(Program("process 1; emit bolt(30, speed(0), ticks(20)); wait ticks(10);"));
    carrier.Origin = At(20.5, 10.5);
    bridge.CastCarrier(world, carrier);
    Check(bridge.Projectiles().size() == 2 && bridge.Projectiles()[0].Version == oldVersion
        && bridge.Projectiles()[0].Damage == 10 && bridge.Projectiles()[1].Damage == 30, "old artifact replaced in flight");
    bridge.Clear(world);
    bridge.BeginFrame();
    bridge.ApplyProcess(Program("process 1; emit dig(cells(0)); while true {}"));
    world.SetCell(10, 10, {CellTypeId::Stone});
    bridge.CastCarrier(world, Carrier());
    Check(bridge.Stats().Roots == 0 && bridge.LastEffects().empty() && bridge.ProcessReceipts().empty()
        && !bridge.ProcessDiagnostics().empty() && world.GetCell(10, 10).Type == CellTypeId::Stone,
        "fault committed unvalidated slice or leaked instance");
}
void WakeAndBounds() {
    World world(64, 64);
    FormulaBridge bridge;
    bridge.ApplyProcess(Program("process 1; await wake within ticks(2); if outcome() == wake { emit dig(cells(0)); }"));
    auto carrier = Carrier(ProcessCarrierKind::NaturalSource);
    bridge.CastCarrier(world, carrier);
    world.SetCell(10, 10, {CellTypeId::Stone});
    for (int i = 0; i < 1000; ++i) bridge.WakeCarrier(carrier.Kind, carrier.Subject);
    Check(bridge.ProcessStats().PendingEvents == 1, "wake queue unbounded");
    bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::Air && bridge.Stats().Roots == 0, "wake failed");

    bridge.BeginFrame();
    bridge.ApplyProcess(Program("process 1; while true { var n = 0; while n < 8 { emit bolt(10, speed(0), ticks(100)); n = n + 1; } wait ticks(1); }"));
    carrier.Credits = 10000;
    bridge.CastCarrier(world, carrier);
    for (int i = 0; i < 40; ++i) bridge.Step(world);
    Check(bridge.Projectiles().size() == 128 && bridge.ProcessStats().PendingEvents <= 1
        && bridge.ProcessReceipts().size() == 256 && bridge.ProcessReceipts().back().Refusal == ProcessRefusal::Capacity,
        "projectile, event or receipt bound failed");
    bridge.Clear(world);
    Check(bridge.Projectiles().empty() && bridge.ProcessStats().Instances == 0, "clear failed");
}
void WindAndFarCoordinates() {
    World world(64, 64);
    WindStrategySettings wind;
    wind.Kind = WindStrategyKind::Coarse;
    wind.Coarse.Columns = wind.Coarse.Rows = 16;
    wind.Coarse.Ambient = {};
    wind.Coarse.GustSpeed = 0;
    world.ConfigureWindStrategy(64, wind);
    const auto pulse = Program("process 1; emit wind(cells(2), speed(60), speed(0)); wait ticks(1);");
    FormulaBridge bridge;
    bridge.ApplyProcess(pulse);
    for (const auto kind : {ProcessCarrierKind::Player, ProcessCarrierKind::NaturalSource, ProcessCarrierKind::FixedFixture}) {
        bridge.CastCarrier(world, Carrier(kind));
        Check(bridge.ProcessReceipts().back().Outcome == process::Outcome::Applied
            && std::holds_alternative<api::WindPulseSubmitted>(bridge.LastEffects()[0].Payload), "wind semantics differed by carrier");
    }
    bridge.Clear(world);
    bridge.BeginFrame();
    wind.Kind = WindStrategyKind::Procedural;
    world.ConfigureWindStrategy(64, wind);
    bridge.CastCarrier(world, Carrier());
    Check(bridge.ProcessReceipts().back().Refusal == ProcessRefusal::WindUnavailable, "unsupported wind backend not refused");
    bridge.Clear(world);

    for (const int64_t far : {int64_t{1} << 55, -(int64_t{1} << 55)}) {
        World sparse;
        testing::WorldAccess::Map(sparse).Insert(std::make_unique<WorldChunk>(CellCoord{far / storage::ChunkEdge, 0}));
        // A production streaming scope is used by the separate gameplay far test;
        // here verify exact bridge conversion within a distant simulation window.
        testing::WorldAccess::SimulationBounds(sparse) = {far, 0, 64, 64};
        auto distant = Carrier();
        distant.Origin = {ChunkOf({far, 0}), {10.25, 10.5}};
        distant.Scope = {far, 0, 64, 64};
        bridge.BeginFrame();
        bridge.ApplyProcess(Program("process 1; emit bolt(10, speed(60), ticks(5)); await hit within ticks(8);"));
        Check(bridge.CastCarrier(sparse, distant), "far activation refused");
        bridge.Step(sparse);
        Check(bridge.Projectiles().front().Position.Cell().X == far + 11
            && std::abs(bridge.Projectiles().front().Position.RelativeTo({far + 11, 10})[0] - .25) < 1e-10,
            "far position lost cell/fraction separation");
        const auto beforeFreeze = bridge.InspectProcesses()[0].State;
        auto unloaded = testing::WorldAccess::Map(sparse).Remove(ChunkOf({far, 0}));
        bridge.Step(sparse);
        Check(bridge.Projectiles().empty() && bridge.ProcessStats().SuspendedInstances == 1
            && bridge.InspectProcesses()[0].State.ActiveSteps == beforeFreeze.ActiveSteps,
              "missing carrier region did not freeze");
        testing::WorldAccess::Map(sparse).Insert(std::move(unloaded));
        bridge.SynchronizeActivity(sparse);
        Check(bridge.Projectiles().size() == 1 && bridge.InspectProcesses()[0].State.ActiveSteps == beforeFreeze.ActiveSteps,
              "reloading lost the frozen carrier or advanced it");
        bridge.Clear(sparse);
    }
}
void SessionIntegration() {
    GameSession session;
    Check(bool(session.ApplyProcess(NativeCaller::Driver, CarrierExample())), "session process apply failed");
    const auto activation = session.Execute(NativeCaller::Input, api::ActivateProgram{session.AppliedProgram(), {{600, 500}, {.5, .5}}});
    Check(activation.Applied() && session.GetFormulaRuntime().Projectiles().size() == 1, "player action did not route process");
    const auto initial = session.GetFormulaRuntime().Projectiles().front().Position;
    session.Tick(.1f, {});
    Check(session.GetFormulaRuntime().Projectiles().front().Position.Cell() == initial.Cell(), "paused process advanced");
    auto view = session.Observe(NativeCaller::Driver, {480, 480, 64, 64});
    Check(view.Snapshot && view.Snapshot->Projectiles.front().Program.ResourceId == 2, "observation mislabeled process artifact");
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(1.f / 60, {});
    Check(session.GetFormulaRuntime().Projectiles().front().Position.Cell() != initial.Cell(), "successful fixed step did not advance process");
    const auto before = session.GetFormulaRuntime().Projectiles().front().Position;
    testing::SessionAccess::RestoreClock(session, session.GetSimulationTime(), .02, true);
    Check(!session.ApplyProcess(NativeCaller::Driver, CarrierExample())
        && !session.Execute(NativeCaller::Input, api::ActivateProgram{session.AppliedProgram(), {{600, 500}, {.5, .5}}}).Applied(),
        "pending step accepted process mutation");
    Check(session.GetFormulaRuntime().Projectiles().front().Position.Cell() == before.Cell(), "refused activation aged process");
    testing::SessionAccess::RestoreClock(session, session.GetSimulationTime(), 0, false);
    session.Control(NativeCaller::Driver, api::ClearRunning{});
    Check(session.GetFormulaRuntime().Stats().Roots == 0, "session clear leaked process");
}
void InvalidHostCoordinates() {
    World world(64, 64);
    FormulaBridge bridge;
    bridge.ApplyProcess(Program("process 1; wait ticks(2);"));
    const auto rejected = [&](ProcessCarrier carrier) {
        bool threw = false;
        try { bridge.CastCarrier(world, carrier); }
        catch (const std::invalid_argument &) { threw = true; }
        catch (const std::out_of_range &) { threw = true; }
        Check(threw && bridge.Stats().Roots == 0, "invalid host coordinate was not rejected before use");
    };
    for (const auto value : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
        auto carrier = Carrier(); carrier.Origin.Local[0] = value; rejected(carrier);
    }
    for (const auto value : {INT64_MIN, INT64_MAX}) {
        auto carrier = Carrier(); carrier.Origin.Chunk.X = value; rejected(carrier);
        carrier = Carrier(); carrier.Scope.X = value; rejected(carrier);
    }
    bridge.ApplyProcess(Program("process 1; emit bolt(10, speed(60), ticks(10)); wait ticks(5);"));
    auto carrier = Carrier(); carrier.AngleDegrees = 1e308;
    Check(bridge.CastCarrier(world, carrier), "finite angle rejected");
    Check(std::isfinite(bridge.Projectiles()[0].Velocity.X) && std::isfinite(bridge.Projectiles()[0].Velocity.Y),
        "large finite angle overflowed rotation");
    bridge.Step(world);
}

void ProcessEditingAndInspection() {
    World world(64, 64);
    FormulaBridge bridge;
    formula::process::editing::Draft draft("editor.process");
    draft.SetSource("process 1; wait ticks(2); emit bolt(1, speed(0), ticks(10)); wait ticks(10);");
    Check(draft.Build(), "editor process compile failed");
    bridge.ApplyProcess(*draft.Program());
    const auto oldVersion = bridge.Version();
    const auto oldIdentity = bridge.ActiveProcess()->Identity();
    Check(bridge.CastCarrier(world, Carrier()), "old draft activation failed");
    auto inspection = bridge.InspectProcesses();
    Check(inspection.size() == 1 && inspection[0].Version == oldVersion
        && inspection[0].State.ActiveSteps == 1 && inspection[0].State.Remaining == 2,
        "debug view did not expose the committed continuation");
    draft.SetSource("process 1; emit bolt(9, speed(0), ticks(10)); wait ticks(10);");
    Check(draft.Build(), "replacement draft compile failed");
    bridge.ApplyProcess(*draft.Program());
    Check(bridge.ActiveProcess()->Identity() == draft.Program()->Identity() && bridge.Version() == oldVersion + 1,
        "Apply did not publish a new version");
    Check(bridge.CastCarrier(world, Carrier()), "replacement activation failed");
    inspection = bridge.InspectProcesses();
    Check(inspection.size() == 2 && inspection[0].Program == oldIdentity && inspection[1].Program == draft.Program()->Identity()
        && inspection[0].State.ActiveSteps == 1, "Apply or new activation advanced/rebound the old process");
    bridge.Step(world); bridge.Step(world);
    Check(bridge.Projectiles().size() == 2 && bridge.Projectiles()[0].Damage == 9 && bridge.Projectiles()[1].Damage == 1,
        "editing changed the existing continuation's effects");
    bridge.Clear(world);
    Check(bridge.InspectProcesses().empty(), "debug view retained cancelled processes");
}
void PersistentFields() {
    World world(64, 64);
    world.FillBaseForce({0, 0});
    FormulaBridge bridge;
    const auto source = "field(cells(4), acceleration(3600), acceleration(0), ticks(20))";
    const auto program = Program(std::string("process 1; emit ") + source +
        "; await result within ticks(1); let h = effect_handle(); "
        "emit update_field(h, field(cells(4), acceleration(0), acceleration(-3600), ticks(10))); "
        "await result within ticks(1); emit stop_field(h); wait ticks(1); emit stop_field(h); wait ticks(1);");
    for (const auto kind : {ProcessCarrierKind::Player, ProcessCarrierKind::NaturalSource,
                           ProcessCarrierKind::FixedFixture, ProcessCarrierKind::Device}) {
        bridge.BeginFrame();
        auto carrier = Carrier(kind);
        carrier.Credits = 2; // Installation + update; cleanup must remain possible at zero.
        bridge.ApplyProcess(program);
        Check(bridge.CastCarrier(world, carrier), "field carrier admission failed");
        const auto &fields = world.GetEnvironment().GetFieldSources();
        Check(fields.Size() == 1 && world.GetForce(10, 10).X == 3600, "field installation has no actual force");
        const auto handle = fields.Sources().front().Handle;
        Check(std::get<api::FieldSourceChanged>(bridge.LastEffects().front().Payload).Handle == handle.Id,
              "installation fact lost its handle");
        bridge.Step(world);
        Check(fields.Size() == 1 && fields.Sources().front().Handle == handle &&
              world.GetForce(10, 10).Y == -3600 && world.GetForce(10, 10).X == 0,
              "handle update replaced identity or accumulated acceleration");
        Check(!world.UpdateFieldSource(bridge.LastActivationId() + 1, handle,
                  {At(10.5, 10.5), 4, {999, 0}, 20}) &&
              !world.StopFieldSource(bridge.LastActivationId() + 1, handle), "foreign owner could mutate source");
        bridge.Step(world);
        Check(fields.Size() == 0 && world.GetForce(10, 10).Y == 0 && bridge.InspectProcesses().front().Credits == 0,
              "zero-credit stop did not remove source");
        bridge.Step(world);
        Check(bridge.ProcessReceipts().back().Refusal == ProcessRefusal::InvalidHandle && fields.Size() == 0,
              "repeated stop revived a handle or changed resources");
        bridge.Step(world);
        Check(bridge.Stats().Roots == 0, "field program failed to retire");
    }
    for (const auto ending : {std::string("stop;"), std::string("emit update_field(effect_handle(), dig(cells(0)));"),
                             std::string("")}) {
        bridge.BeginFrame();
        world.SetCell(10, 10, {CellTypeId::Stone});
        bridge.ApplyProcess(Program(std::string("process 1; emit dig(cells(0)); emit ") + source + "; wait ticks(1); " + ending));
        Check(bridge.CastCarrier(world, Carrier()), "cleanup fixture failed");
        Check(world.GetEnvironment().GetFieldSources().Size() == 1, "cleanup fixture missing source");
        bridge.Step(world);
        Check(world.GetEnvironment().GetFieldSources().Size() == 0 && world.GetCell(10, 10).Type == CellTypeId::Air,
              "termination leaked field or rolled back completed mining");
    }
    bridge.BeginFrame();
    bridge.ApplyProcess(Program(std::string("process 1; emit ") + source + "; wait ticks(100);"));
    Check(bridge.CastCarrier(world, Carrier()), "revocation fixture failed");
    bridge.CancelCarrier(world, ProcessCarrierKind::Player, 1);
    bridge.CancelCarrier(world, ProcessCarrierKind::Player, 1);
    Check(world.GetEnvironment().GetFieldSources().Size() == 0 && bridge.Stats().Roots == 0,
          "repeated revocation left owned field");
    for (const auto field : {"field(cells(17), acceleration(0), acceleration(0), ticks(20))",
                             "field(cells(1), acceleration(36001), acceleration(0), ticks(20))",
                             "field(cells(1), acceleration(0), acceleration(0), ticks(3601))"}) {
        bridge.BeginFrame();
        bridge.ApplyProcess(Program(std::string("process 1; emit ") + field + "; wait ticks(1);"));
        Check(bridge.CastCarrier(world, Carrier()) && bridge.ProcessReceipts().back().Refusal == ProcessRefusal::Geometry &&
              world.GetEnvironment().GetFieldSources().Size() == 0, "field escaped host geometry/lifetime bound");
        bridge.Clear(world);
    }
}

} // namespace
int main() {
    try {
        ThreeCarriers();
        CollisionOrderAndExpiry();
        RefusalsAndResources();
        FrozenQueriesVersionsAndFaults();
        WakeAndBounds();
        WindAndFarCoordinates();
        SessionIntegration();
        InvalidHostCoordinates();
        ProcessEditingAndInspection();
        PersistentFields();
        std::cout << "FormulaBridge contracts passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
