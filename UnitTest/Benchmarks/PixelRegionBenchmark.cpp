#include "../Support/WorldAccess.h"
#include "Runtime/Simulation/World/Environment/FieldResponse.h"
#include "../Support/SimulatedWorld.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <vector>

namespace {
using World = pixel::testing::SimulatedWorld;
using namespace pixel;
using Clock = std::chrono::steady_clock;
constexpr uint32_t Edge = 1024;

struct FullScan {
    std::vector<uint32_t> Winners = std::vector<uint32_t>(size_t(Edge) * Edge);
    std::vector<MaterialMove> Proposals;
    uint64_t Visited = 0;
    void Step(World &world) {
        Proposals.clear();
        std::fill(Winners.begin(), Winners.end(), UINT32_MAX);
        Visited = size_t(Edge) * Edge;
        const auto &map = world.GetMap();
        for (uint32_t y = 0; y < Edge; ++y) {
            for (uint32_t start = 0; start < Edge; start += storage::ChunkEdge) {
                const auto row = map.MaterialRow({start, y}, std::min(Edge - start, storage::ChunkEdge));
                for (uint32_t column = 0; column < row.size(); ++column) {
                    if (row[column] != CellTypes::Dirt)
                        continue;
                    const int64_t x = start + column;
                    const auto direction = FieldResponse::MovementDirection(world.GetFieldVelocity(x, y));
                    const CellCoord destination{x + direction.X, int64_t(y) + direction.Y};
                    if ((direction.X == 0 && direction.Y == 0) || destination.X < 0 || destination.Y < 0 || destination.X >= Edge || destination.Y >= Edge || world.GetCell(destination.X, destination.Y).Type != CellTypes::Empty)
                        continue;
                    const auto target = size_t(destination.Y) * Edge + size_t(destination.X);
                    if (Winners[target] == UINT32_MAX) {
                        Winners[target] = uint32_t(Proposals.size());
                        Proposals.push_back({{x, y}, destination});
                    }
                }
            }
        }
        pixel::testing::WorldAccess::Map(world).CommitMaterialMoves(Proposals);
    }
};

void Prepare(World &world, int scene) {
    if (scene < 2) {
        world.GenerateTerrain(512);
        if (scene == 1)
            world.PlaceDirt(512, 64);
        return;
    }
    world.Fill({});
    auto &map = pixel::testing::WorldAccess::Map(world);
    for (const auto coordinate : map.ChunkCoordinates()) {
        auto &chunk = *map.FindChunk(coordinate);
        for (uint32_t row = 0; row < storage::ChunkEdge; ++row) {
            const auto y = coordinate.Y * storage::ChunkEdge + row;
            if (y >= 0 && y < Edge && y % 2 == 0)
                std::fill_n(chunk.Data().Materials.begin() + size_t(row) * storage::ChunkEdge, storage::ChunkEdge, CellTypes::Dirt);
        }
        chunk.Recount();
    }
    map.Invalidate({0, 0, Edge, Edge});
}

uint64_t Hash(const World &world) {
    uint64_t hash = 1469598103934665603ull;
    const auto view = world.ReadState();
    for (size_t i = 0; i < view.CellCount(); ++i)
        hash = (hash ^ uint32_t(view.AtIndex(i).Type)) * 1099511628211ull;
    return hash;
}

void Measure(std::ostream &out, int scene, bool reference, int rounds) {
    World world(Edge, Edge);
    Prepare(world, scene);
    FullScan full;
    for (int i = 0; i < 8; ++i)
        if (reference)
            full.Step(world);
        else
            world.Tick(1.f / 60);
    std::vector<double> samples;
    uint64_t visited = 0, active = 0;
    for (int i = 0; i < rounds; ++i) {
        const auto begin = Clock::now();
        if (reference)
            full.Step(world);
        else
            world.Tick(1.f / 60);
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - begin).count());
        visited += reference ? full.Visited : world.GetEnvironment().LastVisitedCells();
        active += reference ? 0 : world.GetEnvironment().LastActiveRegions();
    }
    std::sort(samples.begin(), samples.end());
    const auto median = samples[samples.size() / 2];
    const auto p95 = samples[std::min(samples.size() - 1, size_t(samples.size() * .95))];
    out << scene << ',' << (reference ? "full_scan" : "regions") << ',' << median << ',' << p95 << ','
        << visited / uint64_t(rounds) << ',' << active / uint64_t(rounds) << ',' << world.GetMemoryUsage().Get(performance::MemoryComponent::Total).ReservedBytes()
        << ',' << Hash(world) << '\n';
}
} // namespace

int main(int argc, char **argv) {
    try {
        const int rounds = argc > 2 ? std::max(1, std::stoi(argv[2])) : 40;
        std::ofstream file;
        if (argc > 1)
            file.open(argv[1]);
        std::ostream &out = file.is_open() ? file : std::cout;
        out << "scene,mode,median_ms,p95_ms,visited_cells,active_regions,reserved_bytes,hash\n";
        for (int scene = 0; scene < 3; ++scene)
            for (bool reference : {true, false})
                Measure(out, scene, reference, rounds);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
