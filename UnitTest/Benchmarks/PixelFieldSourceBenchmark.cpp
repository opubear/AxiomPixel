#include "World/World.h"
#include "World/Environment/FieldResponse.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
using namespace pixel;
using Clock = std::chrono::steady_clock;
constexpr int Rounds = 240;

double Micros(Clock::time_point begin) {
    return std::chrono::duration<double, std::micro>(Clock::now() - begin).count();
}

void Measure(std::ostream &out, size_t count) {
    World world(256, 256);
    world.FillBaseForce({0, 3600});
    // Uniformly overlapping sources provide a repeatable worst-case composition
    // cost. Counterforce keeps the actual dirt workload stationary across rounds.
    for (int y = 112; y < 144; ++y)
        for (int x = 112; x < 144; ++x)
            world.SetCell(x, y, {CellTypeId::Dirt});
    world.FillRegion({112, 144, 32, 1}, {CellTypeId::Stone});
    for (size_t source = 0; source < count; ++source)
        if (!world.InstallFieldSource(source + 1, {WorldPosition::FromAbsolute(128, 128), 16, {0, -3600.f / float(count)}, 3600}))
            throw std::runtime_error("benchmark source admission failed");
    world.StepEnvironment(0);
    std::vector<double> steps, samples;
    double checksum = 0;
    uint64_t active = 0, visited = 0;
    for (int round = 0; round < Rounds; ++round) {
        auto begin = Clock::now();
        if (!world.StepEnvironment(double(round + 1) * FieldResponse::StepSeconds))
            throw std::runtime_error("benchmark environment deferred");
        steps.push_back(Micros(begin));
        active += world.GetEnvironment().LastActiveRegions();
        visited += world.GetEnvironment().LastVisitedCells();
        begin = Clock::now();
        for (int y = 112; y < 144; ++y)
            for (int x = 112; x < 144; ++x) {
                const auto force = world.GetForce(x, y);
                checksum += force.X + force.Y;
            }
        samples.push_back(Micros(begin) / 1024);
    }
    std::sort(steps.begin(), steps.end());
    std::sort(samples.begin(), samples.end());
    out << count << ',' << steps[Rounds / 2] << ',' << steps[size_t(Rounds * .95)] << ','
        << samples[Rounds / 2] << ',' << samples[size_t(Rounds * .95)] << ','
        << active / Rounds << ',' << visited / Rounds << ',' << sizeof(FieldSources) << ','
        << world.GetMemoryUsage().Get(performance::MemoryComponent::Total).ReservedBytes() << ',' << checksum << '\n';
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::ofstream file;
        if (argc > 1) file.open(argv[1]);
        std::ostream &out = file.is_open() ? file : std::cout;
        out << "sources,step_median_us,step_p95_us,sample_median_us,sample_p95_us,active_regions,visited_cells,source_capacity_bytes,world_reserved_bytes,checksum\n";
        for (const size_t count : {0, 1, 8, 32, 64, 256}) Measure(out, count);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
