#include "../Support/TestAccess.h"
#include "Runtime/Simulation/World/Environment/FieldResponse.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
namespace process = formula::process;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
process::ProgramArtifact Program(std::string_view source) {
    auto result = process::Compile("region-test", source);
    if (const auto *error = std::get_if<process::Diagnostic>(&result)) throw std::runtime_error(error->Message);
    return std::get<process::ProgramArtifact>(result);
}
ProcessCarrier Carrier(ProcessCarrierKind kind = ProcessCarrierKind::NaturalSource) {
    ProcessCarrier result;
    result.Kind = kind;
    result.Origin = WorldPosition::FromAbsolute(20.5, 20.5);
    result.Scope = {0, 0, 64, 64};
    return result;
}
void Initialize(World &world) {
    testing::WorldAccess::Map(world).Insert(std::make_unique<WorldChunk>(CellCoord{0, 0}));
    testing::WorldAccess::SimulationBounds(world) = {0, 0, 64, 64};
    world.FillBaseForceRegion({0, 0, 64, 64}, {});
}
void Move(World &world, FormulaBridge &bridge, int64_t x) {
    testing::WorldAccess::SimulationBounds(world) = {x, 0, 64, 64};
    bridge.SynchronizeActivity(world);
}
void Step(World &world, FormulaBridge &bridge, uint64_t &tick) {
    bridge.BeginFrame();
    bridge.SynchronizeActivity(world);
    Check(world.StepEnvironment(double(tick++) * FieldResponse::StepSeconds), "environment step failed");
    bridge.Step(world);
}

void ContinuationAndOwnedSources() {
    World world;
    Initialize(world);
    world.SetCell(20, 20, {CellTypeId::Stone});
    FormulaBridge bridge;
    bridge.ApplyProcess(Program("process 1; emit dig(cells(0)); "
        "emit field(cells(4), acceleration(3600), acceleration(0), ticks(4)); "
        "await result within ticks(1); let source = effect_handle(); wait ticks(2); "
        "emit update_field(source, field(cells(4), acceleration(-3600), acceleration(0), ticks(2))); "
        "await result within ticks(1); wait ticks(1); emit stop_field(source);"));
    Check(bridge.CastCarrier(world, Carrier()), "source process admission failed");
    const auto initial = bridge.InspectProcesses()[0];
    const auto original = world.GetEnvironment().GetFieldSources().Sources()[0];
    const auto receipts = bridge.ProcessReceipts().size();
    uint64_t tick = 0;
    // The anchor remains inside; the owned disk crosses the new boundary.
    Move(world, bridge, 19);
    Check(bridge.InspectProcesses()[0].Suspended && world.GetForce(20, 20).X == 0,
          "partially clipped field left its owner active");
    world.SetCell(20, 20, {CellTypeId::CopperOre});
    for (int i = 0; i < 8; ++i) Step(world, bridge, tick);
    const auto frozen = bridge.InspectProcesses()[0];
    Check(frozen.State.ActiveSteps == initial.State.ActiveSteps && frozen.State.Remaining == initial.State.Remaining
        && frozen.Credits == initial.Credits && bridge.ProcessStats().PendingEvents == 1
        && bridge.ProcessReceipts().size() == receipts
        && world.GetEnvironment().GetFieldSources().Sources()[0].Remaining == original.Remaining,
          "freeze advanced a continuation, receipt or source lifetime");
    // A different applied artifact must not replace the suspended continuation.
    bridge.ApplyProcess(Program("process 1; emit dig(cells(8));"));
    for (int i = 0; i < 6; ++i) {
        Move(world, bridge, 0);
        Check(!bridge.SynchronizeActivity(world), "repeated activation was not idempotent");
        Check(world.GetEnvironment().GetFieldSources().Size() == 1 && world.GetForce(20, 20).X == 3600,
              "resume duplicated or lost the field");
        Move(world, bridge, 19);
    }
    Move(world, bridge, 0);
    Check(world.GetEnvironment().GetFieldSources().Sources()[0].Handle == original.Handle
        && bridge.InspectProcesses()[0].Version == initial.Version
        && bridge.InspectProcesses()[0].State.ActiveSteps == initial.State.ActiveSteps,
          "resume changed stable identity/version or stepped immediately");
    for (int i = 0; i < 3; ++i) Step(world, bridge, tick);
    Check(world.GetForce(20, 20).X == -3600 && world.GetEnvironment().GetFieldSources().Sources()[0].Handle == original.Handle
        && bridge.ProcessReceipts().size() == receipts + 1 && world.GetCell(20, 20).Type == CellTypeId::CopperOre,
          "saved effect handle failed to update or committed mining was replayed");
    Step(world, bridge, tick);
    Move(world, bridge, 19); // One remaining environment step, exactly at expiry boundary.
    for (int i = 0; i < 5; ++i) Step(world, bridge, tick);
    Check(world.GetEnvironment().GetFieldSources().Sources()[0].Remaining == 1, "frozen final lifetime expired");
    Move(world, bridge, 0);
    Step(world, bridge, tick);
    Check(world.GetEnvironment().GetFieldSources().Size() == 0 && bridge.ProcessStats().Instances == 0,
          "final active boundary did not expire and retire once");
}

void ProjectilesEventsAndCancellation() {
    World world;
    Initialize(world);
    FormulaBridge bridge;
    bridge.ApplyProcess(Program("process 1; emit bolt(1, speed(60), ticks(4)); await hit within ticks(8); "
        "if outcome() == hit { emit dig(cells(1)); }"));
    Check(bridge.CastCarrier(world, Carrier(ProcessCarrierKind::FixedFixture)), "bolt process failed");
    const auto bolt = bridge.Projectiles()[0];
    Move(world, bridge, 64);
    uint64_t tick = 0;
    for (int i = 0; i < 8; ++i) Step(world, bridge, tick);
    Check(bridge.Projectiles().empty() && bridge.ProcessStats().Projectiles == 1, "suspended projectile aged or stayed presented");
    Move(world, bridge, 0);
    Check(bridge.Projectiles()[0].Position.Cell() == bolt.Position.Cell(), "projectile moved while frozen");
    world.SetCell(22, 20, {CellTypeId::Stone});
    Step(world, bridge, tick);
    Step(world, bridge, tick); // Collision is queued for the following boundary.
    Check(bridge.Projectiles().empty() && bridge.ProcessStats().PendingEvents == 1, "collision event missing");
    Move(world, bridge, 64);
    for (int i = 0; i < 6; ++i) Step(world, bridge, tick);
    Check(world.GetCell(22, 20).Type == CellTypeId::Stone, "frozen impact continuation executed");
    Move(world, bridge, 0);
    Step(world, bridge, tick);
    Check(world.GetCell(22, 20).Type == CellTypeId::Air && bridge.ProcessStats().Instances == 0,
          "queued hit lost during freeze");

    bridge.ApplyProcess(Program("process 1; await wake within ticks(2); if outcome() == wake { emit dig(cells(0)); }"));
    world.SetCell(20, 20, {CellTypeId::Stone});
    Check(bridge.CastCarrier(world, Carrier()), "wake process failed");
    Move(world, bridge, 64);
    bridge.WakeCarrier(ProcessCarrierKind::NaturalSource, 1);
    Move(world, bridge, 0);
    Step(world, bridge, tick);
    Check(world.GetCell(20, 20).Type == CellTypeId::Stone, "inactive external wake was delivered");
    bridge.WakeCarrier(ProcessCarrierKind::NaturalSource, 1);
    Move(world, bridge, 64);
    Move(world, bridge, 0);
    Step(world, bridge, tick);
    Check(world.GetCell(20, 20).Type == CellTypeId::Air, "already queued wake was lost");

    bridge.ApplyProcess(Program("process 1; emit field(cells(4), acceleration(3600), acceleration(0), ticks(50)); wait ticks(50);"));
    Check(bridge.CastCarrier(world, Carrier()), "cancel source setup failed");
    Move(world, bridge, 64);
    bridge.CancelCarrier(world, ProcessCarrierKind::NaturalSource, 1);
    bridge.CancelCarrier(world, ProcessCarrierKind::NaturalSource, 1);
    Move(world, bridge, 0);
    Check(bridge.ProcessStats().Instances == 0 && bridge.ProcessStats().PendingEvents == 0
        && world.GetEnvironment().GetFieldSources().Size() == 0, "frozen cancellation resurrected ownership");
}

void ScopeCapacityAndLegacyDelay() {
    World world;
    Initialize(world);
    FormulaBridge bridge;
    bridge.ApplyProcess(Program("process 1; wait ticks(1); emit dig(cells(4)); await result within ticks(1);"));
    Check(bridge.CastCarrier(world, Carrier()), "scope setup failed");
    Move(world, bridge, 19); // Anchor active; requested disk crosses into the frozen part.
    world.SetCell(20, 20, {CellTypeId::Stone});
    uint64_t tick = 0;
    Step(world, bridge, tick);
    Check(bridge.ProcessReceipts().back().Refusal == ProcessRefusal::Scope && world.GetCell(20, 20).Type == CellTypeId::Stone,
          "cross-region immediate effect was partially applied");
    bridge.Clear(world);
    Move(world, bridge, 0);
    bridge.ApplyProcess(Program("process 1; wait ticks(100);"));
    for (int i = 0; i < 16; ++i) {
        bridge.BeginFrame();
        auto carrier = Carrier(); carrier.Subject = uint64_t(i + 1);
        Check(bridge.CastCarrier(world, carrier), "bounded instance setup failed");
    }
    Move(world, bridge, 64);
    bridge.BeginFrame();
    auto remote = Carrier(); remote.Origin = WorldPosition::FromAbsolute(84, 20); remote.Scope = {64, 0, 64, 64};
    Check(!bridge.CastCarrier(world, remote) && bridge.ProcessStats().SuspendedInstances == 16,
          "suspension evaded instance capacity");
    bridge.Clear(world);
    Move(world, bridge, 0);
    Check(bridge.Apply(formula::Formula::Dig(0).After(2)), "legacy fixture failed");
    Check(bridge.Cast(world, Carrier().Origin, 0), "legacy cast failed");
    Move(world, bridge, 64);
    for (int i = 0; i < 8; ++i) Step(world, bridge, tick);
    Move(world, bridge, 0);
    Step(world, bridge, tick);
    Check(world.GetCell(20, 20).Type == CellTypeId::Stone, "legacy delay caught up while frozen");
    Step(world, bridge, tick);
    Check(world.GetCell(20, 20).Type == CellTypeId::Air, "legacy delay did not resume");
}

void Stock(GameSession &session, items::ItemId item, uint32_t quantity) {
    auto &inventory = testing::SessionAccess::Inventory(session);
    const std::array stock{items::ItemAmount{item, quantity}};
    Check(inventory.Add(inventory.Reference(session.PlayerInventory()), stock) == items::Result::Applied, "stock failed");
}
DeviceSnapshot Device(const GameSession &session, uint64_t id) { return session.GetDevices().Find(id)->Value; }
void Step(GameSession &session) {
    session.BeginFrame();
    Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "single step refused");
    session.Tick(1.f / 60, {});
}

void DeviceInvalidation() {
    for (int reason = 0; reason < 4; ++reason) {
        GameSession session;
        auto &world = testing::SessionAccess::World(session);
        auto &map = testing::WorldAccess::Map(world);
        std::vector<std::unique_ptr<WorldChunk>> chunks;
        for (const auto cell : map.ChunkCoordinates()) chunks.push_back(map.Remove(cell));
        map.ResetInfinite();
        for (auto &chunk : chunks) map.Insert(std::move(chunk));
        Stock(session, items::ItemId::Stone, 4);
        Stock(session, items::ItemId::CopperOre, 2);
        Stock(session, items::ItemId::Coal, 2);
        const auto placed = session.Execute(NativeCaller::Driver, api::PlaceDevice{{100, 100}});
        Check(placed.Applied(), "invalidation fixture failed");
        const auto id = std::get<api::DeviceChanged>(placed.Result).Device.Ref.Id;
        Check(session.Execute(NativeCaller::Driver, api::SupplyDevice{Device(session, id).Ref, 2}).Applied(), "supply failed");
        Check(session.Execute(NativeCaller::Driver, api::SetDeviceEnabled{Device(session, id).Ref, true}).Applied(), "enable failed");
        Step(session);
        testing::WorldAccess::SimulationBounds(world) = {128, 0, 896, 1024};
        session.Tick(0, {});
        Check(session.GetFormulaRuntime().InspectProcesses()[0].Suspended, "device did not freeze before invalidation");
        if (reason == 0) {
            Check(session.Execute(NativeCaller::Driver, api::SetDeviceEnabled{Device(session, id).Ref, false}).Applied(),
                  "frozen disable refused");
        } else if (reason == 1) {
            auto &actors = testing::SessionAccess::Actors(session);
            Check(actors.Remove(*actors.Resolve(ActorId{Device(session, id).Owner})), "owner destruction failed");
        } else if (reason == 2) {
            // An internal carrier destruction must also be reconciled if it did
            // not come through the player recycling action.
            auto &devices = testing::SessionAccess::Devices(session);
            devices.Remove(Device(session, id).Ref);
            auto &inventory = testing::SessionAccess::Inventory(session);
            const auto replacement = devices.Create(100, 100, session.GetActors().PlayerSnapshot()->Id.Value, inventory.Create(64));
            Check(replacement.Id != id, "replacement reused durable identity");
        } else {
            world.SetCell(100, 100, {CellTypeId::Stone});
        }
        testing::WorldAccess::SimulationBounds(world) = {0, 0, 1024, 1024};
        session.Tick(0, {});
        Check(session.GetFormulaRuntime().ProcessStats().Instances == 0
            && world.GetEnvironment().GetFieldSources().Size() == 0, "invalid carrier resumed a frozen source");
        if (reason != 2)
            Check(Device(session, id).Instance == 0 && Device(session, id).Fuel == 1 && Device(session, id).Cycles == 1,
                  "carrier invalidation replayed payment or retained the old instance");
        if (reason == 3) Check(Device(session, id).State == DeviceState::WaitingForEnvironment, "blocked restoration lost reason");
    }
}

void DeviceStreamingAndDestruction() {
    const auto directory = std::filesystem::temp_directory_path() / ("pixel-region-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        GameSession session;
        auto &world = testing::SessionAccess::World(session);
        WorldStreamSettings settings;
        settings.Directory = directory;
        settings.WindowEdge = 128;
        settings.SurfaceY = 10000;
        settings.BudgetBytes = 64ull * 1024 * 1024;
        world.EnableStreaming(settings, {64, 64, 128, 128});
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(128, 128));
        Stock(session, items::ItemId::Stone, 4);
        Stock(session, items::ItemId::CopperOre, 2);
        Stock(session, items::ItemId::Coal, 2);
        const auto placed = session.Execute(NativeCaller::Driver, api::PlaceDevice{{100, 100}});
        Check(placed.Applied(), "streaming device placement failed");
        const auto id = std::get<api::DeviceChanged>(placed.Result).Device.Ref.Id;
        Check(session.Execute(NativeCaller::Driver, api::SupplyDevice{Device(session, id).Ref, 2}).Applied(), "supply failed");
        Check(session.Execute(NativeCaller::Driver, api::SetDeviceEnabled{Device(session, id).Ref, true}).Applied(), "enable failed");
        Step(session);
        for (int i = 0; i < 4; ++i) Step(session);
        const auto running = Device(session, id);
        const auto continuation = session.GetFormulaRuntime().InspectProcesses()[0];
        const auto source = world.GetEnvironment().GetFieldSources().Sources()[0];
        const auto fuel = session.GetInventory().Inspect(session.GetDevices().Find(id)->FuelContainer);
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(8192 + 128, 128));
        world.LoadSimulationNow();
        session.Tick(0, {}); // Paused streaming alone synchronizes lifecycle.
        Check(Device(session, id).State == DeviceState::OutsideActiveArea && Device(session, id).Instance == running.Instance
            && session.GetFormulaRuntime().InspectProcesses()[0].Suspended, "paused streaming failed to freeze device");
        for (int i = 0; i < 40; ++i) Step(session);
        Check(session.GetFormulaRuntime().InspectProcesses()[0].State.ActiveSteps == continuation.State.ActiveSteps
            && world.GetEnvironment().GetFieldSources().Sources()[0].Remaining == source.Remaining
            && session.GetInventory().Inspect(session.GetDevices().Find(id)->FuelContainer) == fuel,
              "far region consumed production, delay or fuel");
        // Explicit visual demand can load the old region; it must not activate it.
        auto view = session.CreateReadView();
        session.RefreshReadView(*view, {64, 64, 128, 128});
        world.Save(); // Waits for actual I/O, including loads/saves.
        session.RefreshReadView(*view, {64, 64, 128, 128});
        Check(session.GetFormulaRuntime().InspectProcesses()[0].Suspended
            && world.GetForce(100, 100).X == 0 && Device(session, id).Cycles == 1,
              "visual residency activated distant device");
        const auto beforeObservation = world.GetStreamer()->PendingCount();
        session.Observe(NativeCaller::Driver, {64, 64, 64, 64});
        Check(session.GetFormulaRuntime().InspectProcesses()[0].Suspended
            && world.GetStreamer()->PendingCount() == beforeObservation, "query activated or loaded frozen region");
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(128, 128));
        world.LoadSimulationNow();
        session.Tick(0, {});
        Check(Device(session, id).State == DeviceState::Running && Device(session, id).Instance == running.Instance
            && Device(session, id).Cycles == 1 && Device(session, id).Fuel == 1
            && world.GetEnvironment().GetFieldSources().Sources()[0].Handle == source.Handle
            && world.GetEnvironment().GetFieldSources().Sources()[0].Remaining == source.Remaining,
              "return restarted/charged a paid cycle");
        for (int i = 0; i < 25; ++i) Step(session);
        Check(Device(session, id).Cycles == 1 && Device(session, id).Fuel == 1, "cycle caught up early");
        Step(session);
        Check(Device(session, id).Cycles == 2 && Device(session, id).Fuel == 0, "cycle did not finish after remaining active time");
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(8192 + 128, 128));
        session.Tick(0, {});
        const auto removed = session.Execute(NativeCaller::Driver, api::RemoveDevice{Device(session, id).Ref});
        Check(removed.Applied() && session.GetFormulaRuntime().ProcessStats().Instances == 0
            && world.GetEnvironment().GetFieldSources().Size() == 0, "frozen removal retained source or instance");
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(128, 128));
        world.LoadSimulationNow();
        session.Tick(0, {});
        Check(session.GetDevices().Count() == 0 && world.GetForce(100, 100).X == 0, "removed identity reappeared on load");
    }
    std::filesystem::remove_all(directory);
}
} // namespace
int main() {
    try {
        ContinuationAndOwnedSources();
        ProjectilesEventsAndCancellation();
        ScopeCapacityAndLegacyDelay();
        DeviceInvalidation();
        DeviceStreamingAndDestruction();
        std::cout << "Region activity, frozen ownership and continuation passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
