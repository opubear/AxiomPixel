#include "ProbeMetrics.h"
#include "World/World.h"
#include "World/Environment/FieldResponse.h"
#include "Performance/FrameProfiler.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <stdexcept>

using pixel::World;
using Clock = std::chrono::steady_clock;
constexpr float Step = 1.f / 60;

void Configure(World &world, const std::string &scene) {
    if (scene == "empty" || scene == "sparse" || scene == "falling_slab") {
        world.GenerateTerrain(1024);
        if (scene == "sparse")
            for (uint32_t x = 0; x < 1024; x += 4)
                world.PlaceDirt(x, 64);
        if (scene == "falling_slab")
            for (uint32_t y = 64; y < 320; ++y)
                for (uint32_t x = 0; x < 1024; ++x)
                    world.PlaceDirt(x, y);
    } else {
        world.GenerateTerrain(512);
        if (scene == "horizontal")
            world.FillForceDirection({1, 0});
        if (scene == "zero_force")
            world.FillForceDirection({0, 0});
        if (scene == "bottom_hole")
            world.RemoveDirt(512, 1023);
        if (scene == "terrain_plus_one")
            world.PlaceDirt(512, 400);
    }
    if (scene != "no_backend") {
        pixel::WindStrategySettings wind;
        wind.Kind = pixel::WindStrategyKind::Empty;
        world.ConfigureWindStrategy(424, wind);
    }
}

double BuildCache(const World &world) {
    const auto start = Clock::now();
    const auto state = world.ReadState();
    probe::Destinations.assign(state.Cells.size(), probe::NoDestination);
    for (uint32_t y = 0; y < state.Height; ++y)
        for (uint32_t x = 0; x < state.Width; ++x) {
            const auto d = pixel::FieldResponse::MovementDirection(world.GetFieldVelocity(x, y));
            const int64_t nx = int64_t(x) + d.X, ny = int64_t(y) + d.Y;
            if ((d.X || d.Y) && nx >= 0 && ny >= 0 && nx < state.Width && ny < state.Height)
                probe::Destinations[size_t(y) * state.Width + x] = uint32_t(ny * state.Width + nx);
        }
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

uint64_t Hash(const World &world) {
    uint64_t value = 14695981039346656037ull;
    for (const auto cell : world.ReadState().Cells) {
        for (auto part : {uint32_t(cell.Type), uint32_t(cell.Force.X), uint32_t(cell.Force.Y)})
            value = (value ^ part) * 1099511628211ull;
    }
    return value;
}

void Check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

void Semantics() {
    World chain(1, 1024);
    chain.Fill({pixel::CellTypes::Dirt});
    chain.Tick(Step); // A stable column, eligible for a proposed sleep state.
    chain.RemoveDirt(0, 1023);
    chain.Tick(Step);
    Check(chain.GetCell(0, 0).Type == pixel::CellTypes::Empty &&
          chain.GetCell(0, 1023).Type == pixel::CellTypes::Dirt,
          "bottom edit must wake the complete 1023-cell chain in one step");

    // Row-major chooses (33,0); 32x32 tile-major would choose (31,1).
    World contest(64, 4);
    contest.FillForceDirection({0, 0});
    contest.SetCell(33, 0, {pixel::CellTypes::Dirt, {-1, 1}});
    contest.SetCell(31, 1, {pixel::CellTypes::Dirt, {1, 0}});
    contest.Tick(Step);
    Check(contest.GetCell(33, 0).Type == pixel::CellTypes::Empty &&
          contest.GetCell(31, 1).Type == pixel::CellTypes::Dirt &&
          contest.GetCell(32, 1).Type == pixel::CellTypes::Dirt,
          "chunk traversal must preserve the global row-major priority");

    World force(3, 3);
    force.FillForceDirection({0, 0});
    force.PlaceDirt(1, 1);
    force.Tick(Step);
    force.SetBaseForce(1, 1, {3600, 0});
    Check(force.GetCell(1, 1).Force == pixel::ForceDirection{0, 0}, "legacy force metadata changed");
    force.Tick(Step);
    Check(force.GetCell(2, 1).Type == pixel::CellTypes::Dirt, "continuous force edit must wake a stationary cell");
    std::cout << "PASS long-chain wake, cross-chunk contention, continuous-force edit\n";
}

int main(int argc, char **argv) {
    try {
        const std::string mode = argc > 1 ? argv[1] : "timing";
        const std::string selected = argc > 2 ? argv[2] : "";
        if (mode == "semantics") {
            Semantics();
            return 0;
        }
        const std::array scenes{"default", "no_backend", "empty", "sparse", "falling_slab", "horizontal", "zero_force", "bottom_hole", "terrain_plus_one"};
        if (mode == "counts") {
            std::cout << "scene,scanned,samples,chains,links,moves,max_chain,capacity,growths,hash\n";
            for (const auto *scene : scenes) {
                if (!selected.empty() && selected != scene)
                    continue;
                World world(1024, 1024);
                Configure(world, scene);
                probe::ResetCounters();
                world.Tick(Step);
                std::cout << scene << ",1048576," << probe::Samples << ',' << probe::Chains << ','
                          << probe::Links << ',' << probe::Moves << ',' << probe::MaxChain << ','
                          << probe::Capacity << ',' << probe::Growths << ',' << Hash(world) << '\n';
            }
            return 0;
        }
        std::cout << "scene,round,step,material_ms,tick_ms,cache_build_ms,hash\n";
        for (const auto *scene : scenes) {
            if (!selected.empty() && selected != scene)
                continue;
            if (std::string(scene) == "bottom_hole")
                continue; // This transient is measured by the first-step counters only.
            for (int round = 0; round < 3; ++round) {
                World world(1024, 1024);
                Configure(world, scene);
                const double build = mode == "cached" ? BuildCache(world) : 0;
                for (int i = 0; i < 8; ++i)
                    world.Tick(Step);
                pixel::performance::FrameProfiler profiler(40);
                std::array<double, 40> elapsed{};
                for (int i = 0; i < 40; ++i) {
                    profiler.BeginFrame();
                    const auto start = Clock::now();
                    world.Tick(Step);
                    elapsed[i] = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
                    profiler.EndFrame();
                }
                const auto samples = profiler.Samples();
                const auto hash = Hash(world);
                for (size_t i = 0; i < samples.size(); ++i) {
                    const auto &material = samples[i].Timings[size_t(pixel::performance::Stage::MaterialStep)];
                    Check(material.Calls == 1, "expected exactly one material step");
                    std::cout << scene << ',' << round << ',' << i << ',' << material.InclusiveNs / 1e6 << ','
                              << elapsed[i] << ',' << build << ',' << hash << '\n';
                }
            }
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
