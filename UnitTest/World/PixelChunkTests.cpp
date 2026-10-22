#include "World/Environment/FieldResponse.h"
#include "../Support/SimulatedWorld.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using World = pixel::testing::SimulatedWorld;
using namespace pixel;
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

struct TemporaryWorld {
    std::filesystem::path Path = std::filesystem::temp_directory_path() / ("pixel-chunks-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryWorld() { std::filesystem::create_directories(Path); }
    ~TemporaryWorld() { std::filesystem::remove_all(Path); }
};

void CoordinatesStorageAndPublication() {
    Require(ChunkOf({-1, -1}) == CellCoord{-1, -1} && ChunkOf({-int64_t(storage::ChunkEdge), 0}) == CellCoord{-1, 0},
            "negative chunk division must floor");
    WorldMap map;
    for (const CellCoord coordinate : {CellCoord{-1, -1}, CellCoord{0, 0}, CellCoord{3, -2}})
        map.Insert(std::make_unique<WorldChunk>(coordinate));
    auto *chunk = map.FindChunk({-1, -1});
    Require(map.GetMemoryUsage().ReservedHeapBytes - map.GetMemoryUsage().UsedHeapBytes == map.ChunkCount() * (storage::ChunkReservedBytes - sizeof(ChunkPayload)),
            "map totals lost the aligned chunk reservation");
    Require(reinterpret_cast<uintptr_t>(&chunk->Data()) % storage::ChunkAlignment == 0, "chunk payload is not page aligned");
    Require(sizeof(ChunkPayload) == storage::ChunkCells * 14, "chunk must retain separated compact fields");
    map.Set({-1, -1}, {CellTypes::Dirt, {-1, 0}});
    map.SetForce({-1, -1}, {12, 34});
    map.Set({0, 0}, {CellTypes::Dirt, {0, -1}});
    Require(map.Get({-1, -1}) == CellState{CellTypes::Dirt, {-1, 0}} && map.GetForce({-1, -1}) == ForceVector{12, 34},
            "negative chunk edge did not retain independent material and force");
    WorldSnapshot snapshot;
    snapshot.Update(map, {-64, -64, 128, 128});
    const auto version = snapshot.RegionVersion({-1, -1});
    Require(!snapshot.TryGet({-1, 0}), "missing chunk must remain missing in snapshot");
    map.SetMaterial({-1, -1}, CellTypes::Empty);
    Require(snapshot.TryGet({-1, -1})->Type == CellTypes::Dirt, "edit leaked into published snapshot");
    snapshot.Update(map, {-64, -64, 128, 128});
    Require(snapshot.TryGet({-1, -1})->Type == CellTypes::Empty && snapshot.RegionVersion({-1, -1}) > version,
            "region revision did not publish edited cell");
    const auto before = map.GetMemoryUsage().ReservedBytes();
    map.Remove({-1, -1});
    snapshot.Update(map, {-64, -64, 128, 128});
    Require(!snapshot.TryGet({-1, -1}) && map.GetMemoryUsage().ReservedBytes() + sizeof(ChunkPayload) <= before,
            "unload did not release chunk storage and snapshot region");

    WorldMap other(2, 2);
    other.Set({0, 0}, {CellTypes::Empty});
    snapshot.Update(other, {0, 0, 2, 2});
    Require(snapshot.TryGet({0, 0})->Type == CellTypes::Empty, "new map identity reused old snapshot contents");
    snapshot.Update(other, {0, 0, 64, 64});
    Require(!snapshot.TryGet({2, 0}), "snapshot exposed padding outside finite map bounds");
    WorldPosition distant{{int64_t{1} << 40, -(int64_t{1} << 40)}, {double(storage::ChunkEdge) - .125, .25}};
    distant.Local[0] += .25;
    distant.Normalize();
    Require(distant.Local[0] == .125 && distant.Local[1] == .25 && distant.Chunk.X == (int64_t{1} << 40) + 1,
            "distant fractional position lost precision across chunk edge");
}

// Deliberately simple dense reference. It reads a frozen grid and resolves each
// destination by the first row-major source, without using region activity.
void DenseStep(std::vector<CellState> &cells, const std::vector<ForceDirection> &directions, uint32_t width, uint32_t height) {
    const auto before = cells;
    std::vector<size_t> winner(cells.size(), cells.size());
    for (size_t source = 0; source < before.size(); ++source) {
        if (before[source].Type != CellTypes::Dirt)
            continue;
        const auto direction = directions[source];
        const int64_t x = int64_t(source % width) + direction.X, y = int64_t(source / width) + direction.Y;
        if ((direction.X == 0 && direction.Y == 0) || x < 0 || y < 0 || x >= width || y >= height)
            continue;
        const size_t target = size_t(y) * width + size_t(x);
        if (before[target].Type == CellTypes::Empty && winner[target] == cells.size())
            winner[target] = source;
    }
    for (size_t target = 0; target < winner.size(); ++target)
        if (winner[target] != cells.size()) {
            cells[winner[target]].Type = CellTypes::Empty;
            cells[target].Type = CellTypes::Dirt;
        }
}

void SynchronousReferenceAndSleep() {
    constexpr uint32_t width = 129, height = 97;
    World world(width, height);
    world.Fill({CellTypes::Empty, {0, 0}});
    std::vector<CellState> cells(size_t(width) * height);
    std::vector<ForceDirection> directions(cells.size());
    std::mt19937 random(7381);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            directions[i] = {int32_t(random() % 3) - 1, int32_t(random() % 3) - 1};
            const auto type = random() % 4 == 0 ? CellTypes::Dirt : random() % 31 == 0 ? static_cast<CellTypeId>(20)
                                                                                       : CellTypes::Empty;
            cells[i] = {type, directions[i]};
            world.SetCell(x, y, cells[i]);
        }
    for (size_t step = 0; step < 80; ++step) {
        DenseStep(cells, directions, width, height);
        world.Tick(1.f / 60);
        for (size_t i = 0; i < cells.size(); ++i)
            Require(world.ReadState().AtIndex(i) == cells[i], "active-region synchronous step differs from dense reference");
    }

    World terrain(256, 1024);
    terrain.GenerateTerrain(512);
    terrain.Tick(1.f / 60);
    terrain.Tick(1.f / 60);
    Require(terrain.GetEnvironment().LastVisitedCells() == 0, "stable ground must sleep without material scans");
    terrain.RemoveDirt(63, 514);
    terrain.Tick(1.f / 60);
    Require(terrain.GetCell(63, 513).Type == CellTypes::Empty && terrain.GetCell(63, 514).Type == CellTypes::Dirt && terrain.GetCell(63, 512).Type == CellTypes::Dirt,
            "vacancy must propagate one cell per synchronous step");
    Require(terrain.GetEnvironment().LastVisitedCells() <= 9 * storage::RegionEdge * storage::RegionEdge,
            "single edit woke unrelated material regions");
    terrain.SetBaseForce(1, 512, {0, -3600});
    terrain.Tick(1.f / 60);
    Require(terrain.GetCell(1, 511).Type == CellTypes::Dirt, "continuous force edit failed to wake sleeping material");
}

void FilesBudgetAndFailure() {
    TemporaryWorld files;
    WorldMap map;
    auto original = std::make_unique<WorldChunk>(CellCoord{-1, 0});
    original->Initialize(512);
    map.Insert(std::move(original));
    map.Set({-1, 42}, {CellTypes::Dirt, {-1, 1}});
    map.SetForce({-1, 42}, {17.25f, -99.5f});
    WorldStreamer::WriteChunk(files.Path, *map.FindChunk({-1, 0}));
    auto loaded = WorldStreamer::ReadChunk(files.Path, {-1, 0}, 512);
    WorldMap restored;
    restored.Insert(std::move(loaded));
    Require(restored.Get({-1, 42}) == map.Get({-1, 42}) && restored.GetForce({-1, 42}) == map.GetForce({-1, 42}),
            "chunk file did not restore material and independent force");
    {
        std::ofstream truncated(files.Path / "chunks" / "0_0.bin");
        truncated << "broken";
    }
    bool rejected = false;
    try {
        WorldStreamer::ReadChunk(files.Path, {0, 0}, 512);
    } catch (const std::exception &) {
        rejected = true;
    }
    Require(rejected, "truncated chunk file was accepted");

    WorldStreamSettings settings;
    settings.Directory = files.Path;
    settings.BudgetBytes = 3 * (storage::ChunkReservedBytes + sizeof(WorldChunk) + 128 * 1024);
    WorldStreamer streamer(map, settings);
    streamer.LoadNow({2 * int64_t(storage::ChunkEdge), 0, storage::ChunkEdge, storage::ChunkEdge});
    streamer.Update({2 * int64_t(storage::ChunkEdge), 0, storage::ChunkEdge, storage::ChunkEdge}, {});
    streamer.Wait();
    Require(streamer.RetainedBytes() <= settings.BudgetBytes, "streaming exceeded retained capacity budget");
    map.SetMaterial({2 * int64_t(storage::ChunkEdge), 0}, CellTypes::Dirt);
    std::filesystem::remove_all(files.Path / "chunks");
    {
        std::ofstream blocker(files.Path / "chunks");
        blocker << "block directory";
    }
    rejected = false;
    try {
        streamer.Save();
    } catch (const std::exception &) {
        rejected = true;
    }
    Require(rejected && map.FindChunk({2, 0})->Dirty && map.Get({2 * int64_t(storage::ChunkEdge), 0}).Type == CellTypes::Dirt,
            "failed save lost dirty resident data");
}
void WindOnlyReevaluatesItsCoveredRegions() {
    World world(128, 128);
    world.Fill({CellTypes::Dirt, {0, 1}});
    WindStrategySettings settings;
    settings.Kind = WindStrategyKind::Coarse;
    settings.Coarse.Ambient = {};
    settings.Coarse.GustSpeed = 0;
    settings.Coarse.DirtDrag = 0;
    world.ConfigureWindDomain(64, settings);
    world.Tick(1.f / 60);
    world.Tick(1.f / 60);
    Require(world.GetEnvironment().LastVisitedCells() == 128 * 64 && world.GetEnvironment().LastActiveRegions() == 2,
            "dynamic wind woke sleeping material regions outside its domain");
}

void BudgetDefersAndResumesSimulation() {
    TemporaryWorld files;
    World world;
    WorldStreamSettings settings;
    settings.Directory = files.Path;
    settings.WindowEdge = 64;
    settings.BudgetBytes = 50ull * 1024 * 1024;
    world.EnableStreaming(settings);
    world.FillRegion({0, 0, 64, 64}, {CellTypes::Empty, {0, 1}});
    for (int64_t x = 0; x < 64; ++x)
        world.SetCell(x, 0, {CellTypes::Dirt, {0, 1}});
    world.SetPublicationBytes(settings.BudgetBytes - world.GetStreamer()->RetainedBytes() - 128);
    world.Tick(1.f / 60);
    Require(world.GetEnvironment().IsWaitingForBudget() && world.SimulationTime() == 0 && world.GetCell(0, 0).Type == CellTypes::Dirt,
            "scratch exhaustion must defer an uncommitted fixed step");
    world.SetPublicationBytes(0);
    world.Tick(1.f / 60);
    Require(!world.GetEnvironment().IsWaitingForBudget() && world.GetCell(0, 1).Type == CellTypes::Dirt && std::abs(world.SimulationTime() - 1.0 / 60) < 1e-10,
            "deferred fixed step did not resume exactly once");
}
} // namespace

int main() {
    try {
        CoordinatesStorageAndPublication();
        SynchronousReferenceAndSleep();
        FilesBudgetAndFailure();
        BudgetDefersAndResumesSimulation();
        WindOnlyReevaluatesItsCoveredRegions();
        std::cout << "Chunk, synchronous region and streaming tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
