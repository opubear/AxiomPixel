#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/Construction/DeviceCosts.h"
#include "Formula/Process/Program.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
using namespace pixel::items;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
formula::process::ProgramArtifact DigProgram() {
    auto result = formula::process::Compile("inventory-test", "process 1; emit dig(cells(0)); await result within ticks(2);");
    if (const auto *error = std::get_if<formula::process::Diagnostic>(&result)) throw std::runtime_error(error->Message);
    return std::get<formula::process::ProgramArtifact>(result);
}
void MiningAndConstruction() {
    for (auto caller : {NativeCaller::Input, NativeCaller::Debug, NativeCaller::Driver}) {
        GameSession session;
        auto &world = testing::SessionAccess::World(session);
        auto &inventory = testing::SessionAccess::Inventory(session);
        const auto bag = session.PlayerInventory();
        const std::array materials{CellTypeId::Dirt, CellTypeId::GroundSoil, CellTypeId::GrassSoil,
            CellTypeId::Stone, CellTypeId::CoalOre, CellTypeId::CopperOre, CellTypeId::IronOre,
            CellTypeId::TungstenOre, CellTypeId::GoldOre};
        for (auto material : materials) {
            world.SetCell(20, 20, {material});
            const auto receipt = session.Execute(caller, api::MineRegion{{20, 20}, uint32_t(material)});
            Check(receipt.Applied() && std::get<api::MaterialResult>(receipt.Result).Collected == MiningYield(material),
                  "mining yield disagrees across callers");
        }
        for (int i = 0; i < 4; ++i) {
            world.SetCell(20, 20, {i == 3 ? CellTypeId::CopperOre : CellTypeId::Stone});
            Check(session.Execute(caller, api::MineRegion{{20, 20}, {}}).Applied(), "construction mining failed");
        }
        const auto before = inventory.Inspect(bag);
        const auto reserve = inventory.Reserve(before.Ref, ForceEmitterCost());
        Check(reserve.Status == Result::Applied, "mined materials cannot fund device cost");
        Check(inventory.Release(reserve.Id) == Result::Applied && inventory.Inspect(bag).Quantity == before.Quantity,
              "failed placement preflight must release all stock");
        const auto paid = inventory.Reserve(inventory.Reference(bag), ForceEmitterCost());
        Check(inventory.Consume(paid.Id) == Result::Applied && inventory.Inspect(bag).Quantity == before.Quantity - 6,
              "device cost did not consume exactly six items");
        const auto after = inventory.Inspect(bag);
        Check(inventory.Reserve(after.Ref, ForceEmitterCost()).Status == Result::InsufficientItems && inventory.Inspect(bag) == after,
              "short device cost partially consumed inventory");
    }
}
void CapacityPlacementAndDeduplication() {
    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    auto &inventory = testing::SessionAccess::Inventory(session);
    const auto bag = session.PlayerInventory();
    const auto caller = session.CallerSession(NativeCaller::Driver);
    const api::ActionRequest request{session.WorldEpoch(), 1, api::MineRegion{{520, 512}, 10}};
    Check(session.Submit(caller, request).Applied() && session.Submit(caller, request).Applied(), "mining request failed");
    Check(inventory.Inspect(bag).Quantity == 1, "duplicate mining yielded twice");
    const auto placed = session.Execute(NativeCaller::Input, api::PlaceMaterial{{20, 20}});
    Check(placed.Applied() && inventory.Inspect(bag).Quantity == 0 &&
          std::get<api::MaterialResult>(placed.Result).Consumed == ItemAmount{ItemId::Soil, 1}, "placement did not consume soil");
    Check(session.Execute(NativeCaller::Debug, api::PlaceMaterial{{21, 20}}).Rejection == api::Reason::InsufficientItems &&
          world.GetCell(21, 20).Type == CellTypeId::Air, "missing placement resource mutated terrain");
    const std::array fill{ItemAmount{ItemId::Soil, 4096}};
    inventory.Add(inventory.Reference(bag), fill);
    const auto full = inventory.Inspect(bag);
    const auto stamp = session.CurrentCommit();
    Check(session.Execute(NativeCaller::Input, api::MineRegion{{521, 512}, {}}).Rejection == api::Reason::InventoryFull &&
          world.GetCell(521, 512).Type == CellTypeId::Dirt && inventory.Inspect(bag) == full && session.CurrentCommit() == stamp,
          "full inventory lost terrain or committed a refusal");
    testing::SessionAccess::RestoreClock(session, 0, 1.0 / 60, true);
    Check(session.Execute(NativeCaller::Driver, api::MineRegion{{522, 512}, {}}).Rejection == api::Reason::WaitingForBudget &&
          inventory.Inspect(bag) == full, "pending step changed inventory");
    testing::SessionAccess::RestoreClock(session, 0, 0);
    Check(session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded() && session.PlayerInventory() != bag &&
          inventory.Inspect(session.PlayerInventory()).Quantity == 0, "world reset retained old bag");
}
void FormulaCollectors() {
    {
        World world(64, 64);
        Inventory inventory;
        const auto bag = inventory.Create(1);
        FormulaBridge bridge;
        bridge.BindPlayerInventory(inventory, bag);
        bridge.ApplyProcess(DigProgram());
        for (int i = 0; i < 2; ++i) {
            world.SetCell(20, 20, {CellTypeId::CopperOre});
            Check(bridge.Cast(world, WorldPosition::FromAbsolute(20.5, 20.5), 0), "dig activation failed");
            Check(world.GetCell(20, 20).Type == (i == 0 ? CellTypeId::Air : CellTypeId::CopperOre),
                  "formula lost ore at inventory capacity");
        }
        Check(inventory.Inspect(bag).Stacks == std::vector<Stack>{{ItemId::CopperOre, 1, 0}}, "formula product differs from direct mining");
        Check(bridge.ProcessReceipts().back().Refusal == ProcessRefusal::Capacity, "process capacity refusal missing");
    }
    World world(64, 64);
    Inventory inventory;
    const auto bag = inventory.Create(1);
    FormulaBridge bridge;
    bridge.BindPlayerInventory(inventory, bag);
    bridge.ApplyProcess(DigProgram());
    ProcessCarrier carrier;
    carrier.Kind = ProcessCarrierKind::NaturalSource;
    carrier.Origin = WorldPosition::FromAbsolute(20.5, 20.5);
    carrier.Scope = world.GetSimulationBounds();
    world.SetCell(20, 20, {CellTypeId::Stone});
    Check(bridge.CastCarrier(world, carrier) && world.GetCell(20, 20).Type == CellTypeId::Air && inventory.Inspect(bag).Quantity == 0,
          "natural excavation credited unrelated player");
}
void PartialDiskOrder() {
    World world(64, 64);
    Inventory inventory;
    const auto bag = inventory.Create(1);
    world.SetCell(20, 19, {CellTypeId::Stone});
    world.SetCell(19, 20, {CellTypeId::CopperOre});
    const auto result = FormulaWorldAdapter::Dig(world, WorldPosition::FromAbsolute(20.5, 20.5), 1,
                                                MiningCollector{inventory, bag});
    Check(result.Applied == 1 && result.InventoryFull == 1 && world.GetCell(20, 19).Type == CellTypeId::Air &&
          world.GetCell(19, 20).Type == CellTypeId::CopperOre, "disk capacity must commit cells in y/x order");
}
} // namespace
int main() {
    try {
        MiningAndConstruction(); CapacityPlacementAndDeduplication(); FormulaCollectors(); PartialDiskOrder();
        std::cout << "Pixel inventory gameplay passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
