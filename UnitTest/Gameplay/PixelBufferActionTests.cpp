#include "../Support/TestAccess.h"
#include <chrono>
#include <iostream>

using namespace pixel;
namespace {
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
struct TemporaryWorld {
    std::filesystem::path Directory = std::filesystem::temp_directory_path()
        / ("pixel-buffer-actions-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryWorld() { std::filesystem::remove_all(Directory); }
};

void BudgetRefusalsPreserveDomainTransactions() {
    TemporaryWorld files;
    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    WorldStreamSettings stream;
    stream.Directory = files.Directory;
    stream.WindowEdge = 64;
    stream.SurfaceY = 10000;
    stream.BudgetBytes = 6 * storage::ChunkReservedBytes + 8 * 1024 * 1024;
    world.EnableStreaming(stream, {0, 0, storage::ChunkEdge + 64, 64});
    world.SetCell(10, 10, {CellTypeId::Stone});
    WindStrategySettings wind;
    wind.Kind = WindStrategyKind::Coarse;
    world.ConfigureWindDomain(64, wind);
    auto &inventory = testing::SessionAccess::Inventory(session);
    const auto bag = session.PlayerInventory();
    const items::ItemAmount soil{items::ItemId::Soil, 1};
    inventory.Add(inventory.Reference(bag), {&soil, 1});
    testing::SessionAccess::CommitFixtureChanges(session);
    const auto published = world.ReadBuffers();
    const auto stamp = session.CurrentCommit();
    const auto stock = inventory.Inspect(bag);
    testing::WorldAccess::SaturateBudget(world);
    Check(session.Execute(NativeCaller::Driver, api::MineRegion{{10, 10}, {}}).Rejection == api::Reason::WaitingForBudget
          && inventory.Inspect(bag) == stock && world.GetCell(10, 10).Type == CellTypeId::Stone,
          "mining changed inventory before the world write could be reserved");
    Check(session.Execute(NativeCaller::Driver, api::PlaceMaterial{{11, 10}}).Rejection == api::Reason::WaitingForBudget
          && inventory.Inspect(bag) == stock && world.GetCell(11, 10).Type == CellTypeId::Air,
          "failed placement leaked an inventory reservation");
    Check(session.CurrentCommit() == stamp, "refused material actions changed the commit stamp");
    // Numerical wind storage retains the separate budget of the existing solver;
    // an impulse must still detach from the sealed reader version.
    const auto oldWind = published->SampleWind({20, 20});
    FormulaBridge bridge;
    const auto compiled = formula::process::Compile("budget-wind", "process 1; emit wind(cells(2), speed(60), speed(0)); wait ticks(1);");
    Check(std::holds_alternative<formula::process::ProgramArtifact>(compiled), "wind fixture compilation failed");
    bridge.ApplyProcess(std::get<formula::process::ProgramArtifact>(compiled));
    Check(bridge.Cast(world, WorldPosition::FromAbsolute(20.5, 20.5), 0), "budget wind process did not activate");
    bridge.SynchronizeActivity(world);
    Check(world.ReadBuffers() == published && published->SampleWind({20, 20}) == oldWind && published->GetMap().Get({10, 10}).Type == CellTypeId::Stone,
          "refused action changed a sealed version");

    world.SetPublicationBytes(0);
    testing::WorldAccess::SaturateBudget(world, storage::ChunkReservedBytes + 32 * 1024);
    const auto brush = session.Execute(NativeCaller::Driver,
        api::SpawnElement{{int64_t(storage::ChunkEdge), 20}, uint32_t(CellTypeId::Water), 2});
    Check(brush.Rejection == api::Reason::WaitingForBudget
          && world.GetCell(storage::ChunkEdge - 1, 20).Type == CellTypeId::Air
          && world.GetCell(storage::ChunkEdge + 1, 20).Type == CellTypeId::Air,
          "cross-chunk brush partially wrote before failing its reservation");
    world.SetPublicationBytes(0);
    Check(session.Execute(NativeCaller::Driver, api::MineRegion{{10, 10}, {}}).Applied()
          && inventory.Inspect(bag).Quantity == stock.Quantity + 1,
          "mining did not recover after releasing pressure");
}
}

int main() {
    try { BudgetRefusalsPreserveDomainTransactions(); std::cout << "Buffer action transactions passed\n"; }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
