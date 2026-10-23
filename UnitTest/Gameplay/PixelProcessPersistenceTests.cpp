#include "Gameplay/FormulaBridge/FormulaBridge.h"
#include "World/WorldStorageError.h"
#include "../Support/WorldAccess.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
namespace process = formula::process;
void Check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
process::ProgramArtifact Program(std::string_view source) {
    auto result = process::Compile("checkpoint-test", source);
    if (const auto *error = std::get_if<process::Diagnostic>(&result)) throw std::runtime_error(error->Message);
    return std::get<process::ProgramArtifact>(std::move(result));
}
ProcessCarrier Carrier(double x = 10.5) {
    ProcessCarrier carrier;
    carrier.Kind = ProcessCarrierKind::NaturalSource;
    carrier.Origin = WorldPosition::FromAbsolute(x, 10.5);
    carrier.Scope = {0, 0, 64, 64};
    carrier.Credits = 6;
    return carrier;
}
void RoundTrip(FormulaBridge &bridge, World &world) {
    const auto encoded = bridge.CaptureState();
    bridge.Clear(world);
    bridge.RestoreState(encoded);
    Check(bridge.CaptureState() == encoded, "formula checkpoint changed on round trip");
    Check(bridge.LastEffects().empty(), "restoring checkpoint replayed committed effects");
}

void CommittedMiningAndWaitingReceipt() {
    World world(64, 64);
    world.SetCell(10, 10, {CellTypeId::Stone});
    FormulaBridge bridge;
    bridge.ApplyProcess(Program("process 1; emit dig(cells(0)); await result within ticks(3); "
        "if outcome() == applied { wait ticks(2); }"));
    Check(bridge.CastCarrier(world, Carrier()), "mining process refused");
    Check(world.GetCell(10, 10).Type == CellTypeId::Air, "initial mining did not commit");
    const auto version = bridge.Version();
    const auto before = bridge.InspectProcesses().front();
    world.SetCell(10, 10, {CellTypeId::CopperOre});
    RoundTrip(bridge, world);
    Check(bridge.Version() == version && bridge.LastActivationId() == before.State.Id
        && bridge.ProcessStats().PendingEvents == 1 && bridge.InspectProcesses().front().Credits == 5,
        "checkpoint lost version, credit or pending receipt");
    bridge.Step(world);
    const auto continued = bridge.InspectProcesses().front();
    Check(continued.State.Status == process::Phase::Sleeping && continued.State.Remaining == 2
        && continued.State.LastOutcome == process::Outcome::Applied && continued.Credits == 5
        && world.GetCell(10, 10).Type == CellTypeId::CopperOre && bridge.ProcessReceipts().size() == 1,
        "restore replayed mining/spending or lost admission");
    RoundTrip(bridge, world);
    bridge.Step(world); bridge.Step(world);
    Check(bridge.ProcessStats().Instances == 0, "remaining delay restarted after restore");
}

void PendingAndCachedHit() {
    World world(64, 64);
    world.SetCell(10, 10, {CellTypeId::Stone});
    FormulaBridge bridge;
    bridge.ApplyProcess(Program("process 1; emit bolt(1, speed(0), ticks(4)); await result within ticks(2); "
        "await hit within ticks(3); if outcome() == hit { emit dig(cells(0)); } wait ticks(2);"));
    bridge.CastCarrier(world, Carrier());
    Check(bridge.ProcessStats().PendingEvents == 2 && bridge.Projectiles().empty(), "birth hit setup failed");
    RoundTrip(bridge, world);
    bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::Stone, "restore merged separate await continuations");
    RoundTrip(bridge, world);
    bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::Air && bridge.ProcessReceipts().size() == 2,
        "cached hit did not resume mining exactly once");
    world.SetCell(10, 10, {CellTypeId::CopperOre});
    RoundTrip(bridge, world);
    bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::CopperOre && bridge.ProcessReceipts().size() == 2,
        "completed hit mining replayed after restore");
}

void FrozenProjectileLifetime() {
    World world;
    testing::WorldAccess::Map(world).Insert(std::make_unique<WorldChunk>(CellCoord{0, 0}));
    testing::WorldAccess::SimulationBounds(world) = {0, 0, 64, 64};
    FormulaBridge bridge;
    bridge.ApplyProcess(Program("process 1; emit bolt(1, speed(60), ticks(4)); await result within ticks(2); "
        "await hit within ticks(10); if outcome() == expired { emit dig(cells(0)); }"));
    bridge.CastCarrier(world, Carrier());
    bridge.Step(world);
    const auto position = bridge.Projectiles().front().Position;
    const auto original = bridge.InspectProcesses().front();
    testing::WorldAccess::SimulationBounds(world) = {64, 0, 64, 64};
    bridge.SynchronizeActivity(world);
    RoundTrip(bridge, world);
    Check(bridge.InspectProcesses().front().Suspended && bridge.Projectiles().empty()
        && bridge.ProcessStats().Projectiles == 1, "frozen projectile was lost or presented");
    for (size_t i = 0; i < 8; ++i) bridge.Step(world);
    Check(bridge.InspectProcesses().front().State.ActiveSteps == original.State.ActiveSteps
        && bridge.InspectProcesses().front().State.Remaining == original.State.Remaining,
        "restored frozen process advanced");
    RoundTrip(bridge, world);
    testing::WorldAccess::SimulationBounds(world) = {0, 0, 64, 64};
    bridge.SynchronizeActivity(world);
    const auto resumed = bridge.Projectiles().front().Position;
    Check(resumed.Chunk == position.Chunk && resumed.Local == position.Local, "projectile position restarted after restore");
    world.SetCell(10, 10, {CellTypeId::CopperOre});
    bridge.Step(world); bridge.Step(world);
    Check(bridge.Projectiles().size() == 1, "projectile lifetime expired early");
    RoundTrip(bridge, world);
    bridge.Step(world);
    Check(bridge.Projectiles().empty() && world.GetCell(10, 10).Type == CellTypeId::CopperOre,
        "projectile remaining lifetime restarted or expiry delivered immediately");
    bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::Air && bridge.ProcessStats().Instances == 0,
        "restored expiry did not resume continuation");
}

void ImmutableArtifactsAndIdentities() {
    World world(64, 64);
    world.SetCell(10, 10, {CellTypeId::Stone});
    FormulaBridge bridge;
    const auto old = Program("process 1; wait ticks(3); emit dig(cells(0)); wait ticks(2);");
    const auto newer = Program("process 1; wait ticks(3); emit bolt(2, speed(0), ticks(4)); wait ticks(5);");
    bridge.ApplyProcess(old); bridge.CastCarrier(world, Carrier());
    const auto oldVersion = bridge.Version();
    bridge.ApplyProcess(newer); bridge.CastCarrier(world, Carrier(20.5));
    const auto newVersion = bridge.Version();
    RoundTrip(bridge, world);
    const auto instances = bridge.InspectProcesses();
    Check(instances.size() == 2 && instances[0].Program == old.Identity() && instances[0].Version == oldVersion
        && instances[1].Program == newer.Identity() && instances[1].Version == newVersion
        && bridge.ActiveProcess()->Identity() == newer.Identity() && bridge.Version() == newVersion,
        "old running artifact was rebound to active version");
    Check(bridge.CastCarrier(world, Carrier(30.5)) && bridge.LastActivationId() == 3,
        "restored activation identity was reused");
    for (size_t i = 0; i < 3; ++i) bridge.Step(world);
    Check(world.GetCell(10, 10).Type == CellTypeId::Air && bridge.Projectiles().size() == 2,
        "restored immutable artifacts changed behavior");
}

void TypedLocalsAndHandleInspection() {
    World world(64, 64);
    auto bridge = std::make_unique<FormulaBridge>();
    bridge->ApplyProcess(Program("process 1; let b = true; let i = 2; let n = 1.25; let c = cells(0.125); "
        "let s = speed(-2.5); let t = ticks(3); let a = acceleration(1.5); "
        "let e = field(cells(2), acceleration(2), acceleration(0), ticks(10)); "
        "emit e; await result within ticks(2); let h = effect_handle(); let o = outcome(); "
        "let u = update_field(h, e); let stopEffect = stop_field(h); wait ticks(2); emit u; await result within ticks(2); wait t;"));
    bridge->CastCarrier(world, Carrier());
    bridge->Step(world);
    const auto original = bridge->InspectProcesses().front();
    Check(original.ReferencedFieldHandles.size() == 1, "field handle not exposed for ownership validation");
    // World sources are persisted by the owning domain. Do not cancel this root,
    // which would explicitly remove the source being transferred to the clone.
    const auto encoded = bridge->CaptureState();
    bridge.reset();
    FormulaBridge restored;
    restored.RestoreState(encoded);
    Check(restored.CaptureState() == encoded && restored.InspectProcesses().front().ReferencedFieldHandles
        == original.ReferencedFieldHandles, "typed local values or opaque handles failed exact round trip");
    const auto source = world.GetEnvironment().GetFieldSources().Sources().front();
    restored.Step(world); restored.Step(world);
    Check(world.GetEnvironment().GetFieldSources().Size() == 1
        && world.GetEnvironment().GetFieldSources().Sources().front().Handle == source.Handle,
        "restored local handle duplicated its existing source");
}

void DistantPositionPrecision() {
    for (const auto sign : {-1, 1}) {
        const int64_t far = sign * (int64_t{1} << 49) * storage::ChunkEdge;
        World world;
        testing::WorldAccess::Map(world).Insert(std::make_unique<WorldChunk>(ChunkOf({far, 0})));
        testing::WorldAccess::SimulationBounds(world) = {far, 0, 32, 32};
        auto carrier = Carrier();
        carrier.Origin = {ChunkOf({far, 0}), {10.125, 10.375}};
        carrier.Scope = {far, 0, 32, 32};
        FormulaBridge bridge;
        bridge.ApplyProcess(Program("process 1; emit bolt(1, speed(0), ticks(3)); wait ticks(2);"));
        Check(bridge.CastCarrier(world, carrier), "distant checkpoint carrier refused");
        RoundTrip(bridge, world);
        const auto position = bridge.Projectiles().front().Position;
        Check(position.Cell().X == far + 10 && position.RelativeTo({far + 10, 10}) == std::array<double, 2>{.125, .375},
            "checkpoint rounded distant coordinates or fractional position");
    }
}

void CorruptionAndLegacyPolicy() {
    World world(64, 64);
    FormulaBridge bridge;
    const auto tree = formula::Formula::Bolt(0.12345678901234567, 0.0000000123456789, 3).After(2);
    Check(bridge.Apply(tree), "legacy description setup failed");
    RoundTrip(bridge, world);
    Check(bridge.ActiveFormula().Child(0).Values().Damage == tree.Child(0).Values().Damage
        && bridge.ActiveFormula().Child(0).Values().Speed == tree.Child(0).Values().Speed,
        "legacy selected description lost double precision");
    const auto valid = bridge.CaptureState();
    for (const auto &invalid : {valid.substr(0, valid.size() - 1), valid + "trailing", std::string("invalid\n"),
                               std::string(2 * 1024 * 1024 + 1, '0')}) {
        bool rejected = false;
        try { bridge.RestoreState(invalid); } catch (const std::invalid_argument &) { rejected = true; }
        Check(rejected && bridge.CaptureState() == valid, "corrupt restore mutated the existing bridge");
    }
    bridge.Cast(world, WorldPosition::FromAbsolute(10.5, 10.5), 0);
    bool unsupported = false;
    try { (void)bridge.CaptureState(); } catch (const WorldStorageError &) { unsupported = true; }
    Check(unsupported && bridge.Stats().Roots == 1, "live legacy process was silently omitted from save");
}
} // namespace

int main() {
    try {
        CommittedMiningAndWaitingReceipt(); PendingAndCachedHit(); FrozenProjectileLifetime();
        ImmutableArtifactsAndIdentities(); TypedLocalsAndHandleInspection(); DistantPositionPrecision(); CorruptionAndLegacyPolicy();
        std::cout << "process checkpoint continuation, ownership and rejection tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
