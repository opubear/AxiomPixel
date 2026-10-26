#include "../Support/SimulatedWorld.h"
#include "../Support/WorldAccess.h"
#include "Runtime/Simulation/World/Environment/CellularMaterials.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace pixel;
namespace {
using TestWorld = testing::SimulatedWorld;
void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

std::vector<CellCoord> Find(const World &world, CellTypeId type) {
    std::vector<CellCoord> cells;
    const auto bounds = world.GetSimulationBounds();
    for (uint32_t y = 0; y < bounds.Height; ++y)
        for (uint32_t x = 0; x < bounds.Width; ++x) {
            const CellCoord cell{bounds.X + x, bounds.Y + y};
            const auto value = world.TryGetCell(cell.X, cell.Y);
            if (value && value->Type == type) cells.push_back(cell);
        }
    return cells;
}

void MotionAndDecay() {
    for (const auto type : {CellTypeId::Steam, CellTypeId::Fire}) {
        TestWorld world(128, 128);
        world.FillRegion({54, 80, 20, 6}, {type});
        const auto initial = Find(world, type).size();
        for (int step = 0; step < 24; ++step) world.Tick(1.f / 60);
        const auto cells = Find(world, type);
        Require(!cells.empty() && cells.size() <= initial, "wisps disappeared immediately or duplicated");
        double averageY = 0;
        bool spread = false;
        for (const auto cell : cells) {
            averageY += cell.Y;
            spread |= cell.X < 54 || cell.X >= 74;
        }
        Require(averageY / cells.size() < 75 && spread, "steam/fire failed to rise and spread");
        Require(!BlocksCharacter(type) && !BlocksWind(type) && !IsMineable(type), "wisps obstruct actors/wind or yield mining items");
    }

    TestWorld trapped(16, 16);
    trapped.Fill({CellTypeId::Stone});
    trapped.SetCell(4, 4, {CellTypeId::Fire});
    trapped.SetCell(10, 10, {CellTypeId::Steam});
    for (int step = 0; step < 6000; ++step) trapped.Tick(1.f / 60);
    Require(Find(trapped, CellTypeId::Fire).empty() && Find(trapped, CellTypeId::Steam).empty(),
            "blocked transient cells slept permanently instead of dissipating");
    trapped.Tick(1.f / 60);
    Require(trapped.GetEnvironment().LastActiveRegions() == 0, "expired wisps kept a static region awake");
}

void WallsWindAndWater() {
    World world(16, 16);
    const CellCoord source{8, 8};
    world.SetCell(8, 8, {CellTypeId::Fire});
    for (uint64_t step = 0; step < 128; ++step) {
        for (const float direction : {-10.f, 10.f}) {
            const auto update = cellular::Evaluate(world.GetMap(), world.GetSimulationBounds(), source,
                                                   CellTypeId::Fire, step, {direction, 0});
            if (update.Destination)
                Require(update.Destination->X == (direction > 0 ? 9 : 7) && update.Destination->Y == 7,
                        "horizontal wind did not tilt rising fire");
        }
    }
    world.SetCell(8, 7, {CellTypeId::Stone});
    world.SetCell(7, 8, {CellTypeId::Stone});
    world.SetCell(9, 8, {CellTypeId::Stone});
    for (uint64_t step = 0; step < 128; ++step)
        Require(!cellular::Evaluate(world.GetMap(), world.GetSimulationBounds(), source,
                                   CellTypeId::Fire, step, {10, 0}).Destination,
                "fire escaped diagonally through a closed wall corner");
    world.SetCell(8, 9, {CellTypeId::Water});
    Require(world.StepEnvironment(0), "extinction step failed");
    Require(world.GetCell(8, 8).Type == CellTypeId::Air && Find(world, CellTypeId::Water).size() == 1,
            "water contact failed to extinguish fire conservatively for water");
}

void LedgeAndWakeHalo() {
    TestWorld world(128, 64);
    world.FillRegion({0, 31, 128, 1}, {CellTypeId::Stone});
    world.SetCell(60, 30, {CellTypeId::Water});
    for (int step = 0; step < 3; ++step) world.Tick(1.f / 60);
    Require(world.GetEnvironment().LastActiveRegions() == 0, "still water did not sleep");
    Require(world.TryRemoveMaterial(64, 31) == CellEditResult::Applied, "cannot open the cross-region outlet");
    for (int step = 0; step < 12; ++step) world.Tick(1.f / 60);
    const auto water = Find(world, CellTypeId::Water);
    Require(water.size() == 1 && water.front().Y > 31, "opening a four-cell-away outlet did not wake/drain water");
}

void CrossChunkAndFrozenBoundary() {
    World world;
    auto &map = testing::WorldAccess::Map(world);
    for (int y : {-1, 0})
        for (int x : {-1, 0})
            map.Insert(std::make_unique<WorldChunk>(CellCoord{x, y}));
    testing::WorldAccess::SimulationBounds(world) = {-64, -64, 128, 128};
    world.SetCell(-1, -1, {CellTypeId::Water});
    world.SetCell(1, 0, {CellTypeId::Steam});
    world.SetCell(5, 0, {CellTypeId::Fire});
    // Steam is allowed an idle step. All three must eventually cross Y=0.
    for (int step = 0; step < 10; ++step)
        Require(world.StepEnvironment(step / 60.0), "cross-chunk simulation failed");
    Require(Find(world, CellTypeId::Water).front().Y >= 0, "water failed to cross a negative chunk seam");
    for (const auto type : {CellTypeId::Steam, CellTypeId::Fire}) {
        const auto cells = Find(world, type);
        Require(!cells.empty() && cells.front().Y < 0, "rising element failed to cross a chunk seam");
    }
    if (!map.FindChunk(ChunkOf({20, 64})))
        map.Insert(std::make_unique<WorldChunk>(ChunkOf({20, 64})));
    world.SetCell(20, 63, {CellTypeId::Water});
    world.SetCell(20, 64, {CellTypeId::Fire});
    for (int step = 10; step < 100; ++step)
        world.StepEnvironment(step / 60.0);
    Require(world.GetCell(20, 63).Type == CellTypeId::Water && world.GetCell(20, 64).Type == CellTypeId::Fire,
            "simulation boundary moved or decayed frozen cells");
}

void PersistenceAndContinuation() {
    const auto directory = std::filesystem::temp_directory_path()
        / ("pixel-elements-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    World original(128, 128), restored(128, 128);
    original.FillRegion({10, 40, 20, 4}, {CellTypeId::Water});
    original.FillRegion({50, 80, 20, 4}, {CellTypeId::Steam});
    original.FillRegion({90, 80, 20, 4}, {CellTypeId::Fire});
    original.SetBaseForce(51, 70, {123, 456});
    for (int step = 0; step < 17; ++step) original.StepEnvironment(step / 60.0);
    auto &target = testing::WorldAccess::Map(restored);
    for (const auto coordinate : original.GetMap().ChunkCoordinates()) {
        WorldStreamer::WriteChunk(directory, *original.GetMap().FindChunk(coordinate));
        target.Remove(coordinate);
        target.Insert(WorldStreamer::ReadChunk(directory, coordinate, 10000));
    }
    for (int step = 17; step < 50; ++step) {
        Require(original.StepEnvironment(step / 60.0) && restored.StepEnvironment(step / 60.0), "continuation step failed");
        for (const auto type : {CellTypeId::Water, CellTypeId::Steam, CellTypeId::Fire})
            Require(Find(original, type) == Find(restored, type), "saved material or deterministic continuation differs");
    }
    Require(restored.GetBaseForceField().Get(51, 70) == ForceVector{123, 456}, "material motion transported spatial force");
    std::filesystem::remove_all(directory);
}
} // namespace

int main() {
    try {
        MotionAndDecay();
        WallsWindAndWater();
        LedgeAndWakeHalo();
        CrossChunkAndFrozenBoundary();
        PersistenceAndContinuation();
        std::cout << "Element motion, decay, wind, walls, wake halos, chunk boundaries and persistence passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
