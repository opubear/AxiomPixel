#include "../Support/TestAccess.h"
#include "Gameplay/Construction/DeviceCosts.h"
#include "LLMView/JsonAdapter.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
using namespace pixel::items;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void Stock(GameSession &session, ItemId item, uint32_t quantity) {
    auto &inventory = testing::SessionAccess::Inventory(session);
    const std::array amounts{ItemAmount{item, quantity}};
    Check(inventory.Add(inventory.Reference(session.PlayerInventory()), amounts) == Result::Applied, "fixture stock failed");
}
void Fund(GameSession &session) { Stock(session, ItemId::Stone, 4); Stock(session, ItemId::CopperOre, 2); }
DeviceSnapshot Current(GameSession &session, uint64_t id) { return session.GetDevices().Find(id)->Value; }
DeviceSnapshot Place(GameSession &session, api::Cell cell = {100, 100}) {
    Fund(session);
    const auto receipt = session.Execute(NativeCaller::Driver, api::PlaceDevice{cell});
    Check(receipt.Applied(), "fixture device placement failed");
    return std::get<api::DeviceChanged>(receipt.Result).Device;
}
void Step(GameSession &session) {
    session.BeginFrame();
    Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "step control failed");
    session.Tick(1.f / 60, {});
}
void Enable(GameSession &session, uint64_t id, uint32_t coal = 2) {
    Stock(session, ItemId::Coal, coal);
    Check(session.Execute(NativeCaller::Input, api::SupplyDevice{Current(session, id).Ref, coal}).Applied(), "fuel supply failed");
    Check(session.Execute(NativeCaller::Input, api::SetDeviceEnabled{Current(session, id).Ref, true}).Applied(), "enable failed");
}
void AtomicPlacement() {
    GameSession session;
    auto &inventory = testing::SessionAccess::Inventory(session);
    const auto bag = session.PlayerInventory();
    const auto empty = inventory.Inspect(bag);
    const auto stamp = session.CurrentCommit();
    Check(session.Execute(NativeCaller::Input, api::PlaceDevice{{100, 100}}).Rejection == api::Reason::InsufficientItems &&
          inventory.Inspect(bag) == empty && session.GetDevices().Count() == 0 && session.CurrentCommit() == stamp,
          "missing cost changed the session");
    Fund(session);
    auto funded = inventory.Inspect(bag);
    const auto player = session.GetPlayer()->GetWorldPosition().Cell();
    for (const api::Cell target : {api::Cell{100, 512}, api::Cell{player.X, player.Y}})
        Check(session.Execute(NativeCaller::Input, api::PlaceDevice{target}).Rejection == api::Reason::Blocked &&
              inventory.Inspect(bag) == funded && session.GetDevices().Count() == 0, "occupied placement consumed cost");
    Check(session.Execute(NativeCaller::Input, api::PlaceDevice{{1023, 100}}).Rejection == api::Reason::NotReady &&
          inventory.Inspect(bag) == funded, "partial resident footprint consumed cost");
    const auto caller = session.CallerSession(NativeCaller::Driver);
    const api::ActionRequest request{session.WorldEpoch(), 100, api::PlaceDevice{{100, 100}}};
    Check(session.Submit(0, request).Rejection == api::Reason::CallerExpired && inventory.Inspect(bag) == funded,
          "unauthenticated placement changed inventory");
    Check(session.Submit(caller, request).Applied() && session.Submit(caller, request).Applied() &&
          session.GetDevices().Count() == 1 && inventory.Inspect(bag).Quantity == 0, "duplicate placement charged twice");
    for (int y = 100; y < 102; ++y)
        for (int x = 100; x < 102; ++x)
            Check(session.GetWorld().GetCell(x, y).Type == CellTypeId::Air && session.GetDevices().FindAt(x, y),
                  "device was encoded as terrain or lost footprint");
    Fund(session);
    Stock(session, ItemId::Soil, 1);
    funded = inventory.Inspect(bag);
    Check(session.Execute(NativeCaller::Debug, api::PlaceDevice{{101, 101}}).Rejection == api::Reason::Blocked &&
          session.Execute(NativeCaller::Input, api::PlaceMaterial{{101, 101}}).Rejection == api::Reason::Blocked &&
          inventory.Inspect(bag) == funded, "construction overlap consumed inventory");
    testing::SessionAccess::RestoreClock(session, 0, 1.0 / 60, true);
    Check(session.Execute(NativeCaller::Input, api::PlaceDevice{{104, 100}}).Rejection == api::Reason::WaitingForBudget &&
          inventory.Inspect(bag) == funded, "pending step admitted construction");
}

void OwnershipCapacityAndRecycling() {
    GameSession session;
    auto &inventory = testing::SessionAccess::Inventory(session);
    auto &devices = testing::SessionAccess::Devices(session);
    const auto foreign = devices.Create(110, 100, UINT64_MAX, inventory.Create(64));
    for (api::Action action : {api::Action{api::SupplyDevice{foreign}}, api::Action{api::RemoveDevice{foreign}},
                              api::Action{api::SetDeviceEnabled{foreign, true}}})
        Check(session.Execute(NativeCaller::Debug, action).Rejection == api::Reason::PermissionDenied, "foreign device permitted");
    const auto device = Place(session);
    Stock(session, ItemId::Coal, 65);
    Check(session.Execute(NativeCaller::Input, api::SupplyDevice{device.Ref, 64}).Applied(), "fuel port capacity wrong");
    const auto bag = session.PlayerInventory();
    const auto before = inventory.Inspect(bag);
    Check(session.Execute(NativeCaller::Input, api::SupplyDevice{device.Ref}).Rejection == api::Reason::PreconditionFailed &&
          session.Execute(NativeCaller::Input, api::SupplyDevice{Current(session, device.Ref.Id).Ref}).Rejection == api::Reason::InventoryFull &&
          inventory.Inspect(bag) == before, "stale/full supply consumed fuel");
    Stock(session, ItemId::Soil, 4095);
    const auto full = inventory.Inspect(bag);
    Check(session.Execute(NativeCaller::Input, api::RemoveDevice{Current(session, device.Ref.Id).Ref}).Rejection == api::Reason::InventoryFull &&
          inventory.Inspect(bag) == full && Current(session, device.Ref.Id).Fuel == 64, "partial recycling lost device or fuel");
    const std::array space{ItemAmount{ItemId::Soil, 100}};
    const auto reservation = inventory.Reserve(inventory.Reference(bag), space);
    Check(inventory.Consume(reservation.Id) == Result::Applied, "fixture free capacity failed");
    const auto ref = Current(session, device.Ref.Id).Ref;
    const auto count = inventory.Count();
    const auto receipt = session.Execute(NativeCaller::Driver, api::RemoveDevice{ref});
    Check(receipt.Applied() && !devices.Find(ref.Id) && inventory.Count() == count - 1 &&
          inventory.Inspect(bag).Quantity == 4066, "recycling did not return cost and unused fuel");
    Check(session.Submit(receipt.Key.Caller, {receipt.Key.WorldEpoch, receipt.Key.Sequence, api::RemoveDevice{ref}}).Applied() &&
          inventory.Inspect(bag).Quantity == 4066, "duplicate removal refunded twice");
    Check(session.Execute(NativeCaller::Driver, api::RemoveDevice{ref}).Rejection == api::Reason::PreconditionFailed,
          "removed device reference remained valid");
    const auto replacement = session.Execute(NativeCaller::Input, api::PlaceDevice{{100, 100}});
    Check(replacement.Applied() && std::get<api::DeviceChanged>(replacement.Result).Device.Ref.Id != ref.Id,
          "removed footprint stayed occupied or identity reused");
}

void WorkAndLifecycle() {
    GameSession session;
    auto device = Place(session);
    const auto id = device.Ref.Id;
    const auto program = session.AppliedProgram();
    Enable(session, id);
    Step(session);
    Check(Current(session, id).State == DeviceState::WaitingForEnvironment && Current(session, id).Fuel == 2 &&
          session.GetFormulaRuntime().Stats().Roots == 0, "unsupported wind charged fuel");
    Check(session.Execute(NativeCaller::Driver, api::SelectWindStrategy{api::WindKind::Coarse}).Applied(), "wind setup failed");
    const auto before = session.FactHighWatermark();
    Step(session);
    device = Current(session, id);
    Check(device.State == DeviceState::Running && device.Fuel == 1 && device.Pulses == 1 && device.Instance &&
          session.AppliedProgram() == program, "device pulse failed or changed player program");
    const auto running = session.GetFormulaRuntime().InspectProcesses();
    Check(running.size() == 1 && running.front().Carrier == ProcessCarrierKind::Device && running.front().Subject == id,
          "device instance has no owning carrier");
    const auto facts = session.ReadFacts(session.WorldEpoch(), before, session.FactHighWatermark());
    Check(facts.Facts.size() == 2 && std::holds_alternative<api::WindPulseSubmitted>(facts.Facts[0].Payload) &&
          std::get<api::ProgramEffectCause>(facts.Facts[0].Cause).Program == api::ProgramRef{3, 1} &&
          std::get<api::DeviceCause>(facts.Facts[1].Cause).DeviceId == id, "device facts lost ownership or pulse");
    const auto snapshot = device;
    session.Tick(.1f, {});
    Check(Current(session, id) == snapshot, "paused device advanced");
    for (int i = 0; i < 29; ++i) Step(session);
    Check(Current(session, id).Pulses == 1 && Current(session, id).Fuel == 1, "device fired before its wait elapsed");
    Step(session);
    Check(Current(session, id).Pulses == 2 && Current(session, id).Fuel == 0, "device did not restart at its next cycle");
    Check(bool(session.ApplyProgram(NativeCaller::Driver, formula::Formula::Dig(0))), "player program change failed");
    Check(Current(session, id).Pulses == 2 && session.GetFormulaRuntime().HasCarrier(ProcessCarrierKind::Device, id),
          "player Apply replaced the installed device program");
    const auto removed = session.Execute(NativeCaller::Input, api::RemoveDevice{Current(session, id).Ref});
    Check(removed.Applied() && !session.GetFormulaRuntime().HasCarrier(ProcessCarrierKind::Device, id) &&
          session.GetFormulaRuntime().ProcessStats().PendingEvents == 0, "device destruction orphaned runtime");
    Check(llm::ToJson(removed).at("result").at("event") == "removed", "device receipt JSON missing");
    device = Place(session);
    Enable(session, device.Ref.Id, 1);
    Step(session);
    Check(session.Control(NativeCaller::Driver, api::ClearRunning{}).Succeeded() &&
          !Current(session, device.Ref.Id).Enabled && session.GetFormulaRuntime().Stats().Roots == 0,
          "clear running left auto-restarting device");
    Step(session);
    Check(Current(session, device.Ref.Id).Pulses == 1, "cleared device restarted");
    Check(session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded() && session.GetDevices().Count() == 0 &&
          session.GetInventory().Count() == 1, "world replacement retained device or port");
}

void EffectCapacityAndSuspension() {
    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    auto &map = testing::WorldAccess::Map(world);
    std::vector<std::unique_ptr<WorldChunk>> chunks;
    for (auto coordinate : map.ChunkCoordinates()) chunks.push_back(map.Remove(coordinate));
    map.ResetInfinite();
    for (auto &chunk : chunks) map.Insert(std::move(chunk));
    const auto id = Place(session).Ref.Id;
    Enable(session, id, 1);
    Check(session.Execute(NativeCaller::Driver, api::SelectWindStrategy{api::WindKind::Coarse}).Applied(), "wind setup failed");
    std::string source = "process 1; ";
    for (int i = 0; i < 16; ++i) source += "emit wind(cells(2), speed(60), speed(0)); ";
    source += "wait ticks(30);";
    auto compiled = formula::process::Compile("device-capacity-test", source);
    Check(std::holds_alternative<formula::process::ProgramArtifact>(compiled), "capacity fixture program invalid");
    Check(bool(session.ApplyProcess(NativeCaller::Driver, std::get<formula::process::ProgramArtifact>(compiled))), "capacity program apply failed");
    ProcessCarrier carrier;
    carrier.Kind = ProcessCarrierKind::FixedFixture;
    carrier.Subject = id; // Same numeric identity must remain an independent owner.
    carrier.Origin = WorldPosition::FromAbsolute(200, 100);
    carrier.Scope = session.GetWorld().GetSimulationBounds();
    session.BeginFrame();
    Check(session.StartProcessFixture(NativeCaller::Driver, carrier).Succeeded(), "wind budget fixture failed");
    const auto fuel = session.GetDevices().Find(id)->FuelContainer;
    const auto stock = session.GetInventory().Inspect(fuel);
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(1.f / 60, {}); // Stay in the frame whose effect budget was consumed.
    Check(Current(session, id).State == DeviceState::WaitingForCapacity && !Current(session, id).Instance &&
          Current(session, id).Pulses == 0 && session.GetInventory().Inspect(fuel) == stock,
          "effect capacity rejection charged fuel or left an instance");
    Step(session);
    Check(Current(session, id).State == DeviceState::Running && Current(session, id).Pulses == 1,
          "device did not recover when effect budget became available");
    testing::WorldAccess::SimulationBounds(testing::SessionAccess::World(session)) = {128, 0, 896, 1024};
    Step(session);
    Check(Current(session, id).State == DeviceState::OutsideActiveArea && !Current(session, id).Instance &&
          !session.GetFormulaRuntime().HasCarrier(ProcessCarrierKind::Device, id) &&
          session.GetFormulaRuntime().HasCarrier(ProcessCarrierKind::FixedFixture, id),
          "leaving simulation did not clear only the owned instance");
    testing::WorldAccess::SimulationBounds(testing::SessionAccess::World(session)) = {0, 0, 1024, 1024};
    Step(session);
    Check(Current(session, id).State == DeviceState::NeedsFuel && Current(session, id).Pulses == 1,
          "return to simulation replayed a paid pulse");
}

void ResolvedWindOutput() {
    GameSession session, control;
    const auto id = Place(session).Ref.Id;
    Enable(session, id, 1);
    for (auto *value : {&session, &control}) {
        Check(value->Execute(NativeCaller::Driver, api::SelectWindStrategy{api::WindKind::Coarse}).Applied(), "wind setup failed");
        Step(*value);
    }
    bool changed = false;
    const auto bounds = session.GetWorld().GetWindBounds();
    for (uint32_t y = 0; y < bounds.Height; ++y)
        for (uint32_t x = 0; x < bounds.Width; ++x) {
            const auto a = session.GetWorld().SampleWind(CellCoord{bounds.X + x, bounds.Y + y});
            const auto b = control.GetWorld().SampleWind(CellCoord{bounds.X + x, bounds.Y + y});
            changed |= a.X != b.X || a.Y != b.Y;
        }
    Check(Current(session, id).Pulses == 1 && changed, "paid device pulse did not change any sampled wind velocity");

    const auto blocked = Place(control);
    Enable(control, blocked.Ref.Id, 1);
    auto &world = testing::SessionAccess::World(control);
    auto wind = world.GetWindSettings();
    wind.Coarse.Columns = wind.Coarse.Rows = 4;
    world.ConfigureWindDomain(424, wind);
    const auto fuel = control.GetDevices().Find(blocked.Ref.Id)->FuelContainer;
    const auto stock = control.GetInventory().Inspect(fuel);
    Step(control);
    Check(Current(control, blocked.Ref.Id).State == DeviceState::OutputUnresolved &&
          !Current(control, blocked.Ref.Id).Instance && Current(control, blocked.Ref.Id).Pulses == 0 &&
          control.GetInventory().Inspect(fuel) == stock, "unresolved output consumed coal or claimed to work");
    wind.Coarse.Columns = 32;
    wind.Coarse.Rows = 16;
    world.ConfigureWindDomain(424, wind);
    Step(control);
    Check(Current(control, blocked.Ref.Id).State == DeviceState::Running && Current(control, blocked.Ref.Id).Pulses == 1,
          "device did not resume after grid resolution became sufficient");
}
} // namespace
int main() {
    try {
        AtomicPlacement(); OwnershipCapacityAndRecycling(); WorkAndLifecycle(); EffectCapacityAndSuspension(); ResolvedWindOutput();
        std::cout << "Device construction, ports and runtime passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
