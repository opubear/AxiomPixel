#include "../Support/SimulatedWorld.h"
#include <iostream>
#include <stdexcept>

using namespace pixel;
namespace {
using World = pixel::testing::SimulatedWorld;
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

size_t WaterCount(const World &world) {
    const auto view = world.ReadState();
    size_t count = 0;
    for (size_t i = 0; i < view.CellCount(); ++i)
        count += view.AtIndex(i).Type == CellTypeId::Water;
    return count;
}

void StillPoolAndDrain() {
    World world(64, 64);
    world.FillRegion({0, 40, 64, 1}, {CellTypeId::Stone});
    world.FillRegion({0, 20, 1, 20}, {CellTypeId::Stone});
    world.FillRegion({63, 20, 1, 20}, {CellTypeId::Stone});
    world.FillRegion({1, 30, 62, 10}, {CellTypeId::Water});
    const auto count = WaterCount(world);
    for (int i = 0; i < 600; ++i) world.Tick(1.f / 60);
    Require(WaterCount(world) == count, "still pool changed water volume");
    for (int y = 30; y < 40; ++y)
        for (int x = 1; x < 63; ++x)
            Require(world.GetCell(x, y).Type == CellTypeId::Water, "level pool did not settle");
    Require(world.GetEnvironment().LastActiveRegions() == 0, "still water failed to sleep");
    Require(world.TryRemoveMaterial(32, 40) == CellEditResult::Applied, "lake floor cannot be mined");
    for (int i = 0; i < 90; ++i) world.Tick(1.f / 60);
    size_t drained = 0;
    CellCoord water;
    for (int y = 41; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (world.GetCell(x, y).Type == CellTypeId::Water) {
                ++drained;
                water = {x, y};
            }
    Require(WaterCount(world) == count && drained > 0, "opening lake floor failed to drain conservatively");
    Require(world.TryPlaceDirt(water.X, water.Y) == CellEditResult::Blocked, "placing dirt deleted water");
    Require(world.TryRemoveMaterial(water.X, water.Y) == CellEditResult::Blocked, "mining silently deleted water");
}

void MixedConflictAndSpaceForces() {
    World world(8, 8);
    world.SetCell(2, 1, {CellTypeId::Dirt});
    world.SetCell(1, 2, {CellTypeId::Water});
    world.SetCell(0, 2, {CellTypeId::Stone});
    world.SetCell(1, 3, {CellTypeId::Stone});
    world.SetBaseForce(1, 2, {123, 456});
    world.Tick(1.f / 60);
    Require(world.GetCell(2, 2).Type == CellTypeId::Dirt && world.GetCell(1, 2).Type == CellTypeId::Water,
            "water/dirt target conflict lost step-start ordering or material identity");
    Require(WaterCount(world) == 1, "mixed conflict lost water");
    world.SetCell(1, 3, {});
    world.Tick(1.f / 60);
    Require(world.GetCell(1, 3).Type == CellTypeId::Water, "water was converted to dirt during commit");
    Require(world.GetBaseForceField().Get(1, 2) == ForceVector{123, 456}, "water moved a spatial force");
}

void CharacterInWater() {
    World world(64, 64);
    world.FillRegion({0, 10, 64, 54}, {CellTypeId::Water});
    world.SpawnPlayer(20, 20);
    for (int i = 0; i < 120; ++i) world.Tick(1.f / 60);
    const auto floating = world.GetPlayer()->GetWorldPosition().RelativeTo({0, 0});
    Require(floating[1] < 20 && !world.GetPlayer()->Grounded, "water blocks the player or buoyancy is absent");
    world.SetPlayerInput(1, true);
    world.Tick(1.f / 60);
    const auto swimming = world.GetPlayer()->GetWorldPosition().RelativeTo({0, 0});
    Require(swimming[0] > floating[0] && swimming[1] < floating[1], "player cannot move/swim upward");
}

void UnevenWaterMustSettle() {
    for (int height : {2, 3, 6}) {
        World world(4, 10);
        world.FillRegion({0, 0, 1, 10}, {CellTypeId::Stone});
        world.FillRegion({3, 0, 1, 10}, {CellTypeId::Stone});
        world.FillRegion({0, 9, 4, 1}, {CellTypeId::Stone});
        world.FillRegion({1, uint32_t(9 - height), 1, uint32_t(height)}, {CellTypeId::Water});
        for (int i = 0; i < 600; ++i) world.Tick(1.f / 60);
        Require(WaterCount(world) == size_t(height), "uneven water lost volume");
        Require(world.GetCell(1, 8).Type == CellTypeId::Water && world.GetCell(2, 8).Type == CellTypeId::Water,
                "water column oscillated instead of filling both bottom cells");
        Require(world.GetEnvironment().LastActiveRegions() == 0, "uneven water did not settle and sleep");
    }
}
} // namespace

int main() {
    try {
        StillPoolAndDrain();
        MixedConflictAndSpaceForces();
        CharacterInWater();
        UnevenWaterMustSettle();
        std::cout << "Liquid conservation, settling, mining and swimming passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
