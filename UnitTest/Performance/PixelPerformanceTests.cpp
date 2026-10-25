#include "Dev/Debug/Performance/FrameProfiler.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace pixel::performance;
namespace {
uint64_t Time = 0, ClockReads = 0;
FrameProfiler::Clock::time_point Now() {
    ++ClockReads;
    return FrameProfiler::Clock::time_point(std::chrono::nanoseconds(Time));
}
void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void NestedFrame() {
    FrameProfiler profiler(2, Now);
    profiler.BeginFrame();
    Time += 1'000'000;
    {
        Scope parent(Stage::GameTick);
        Time += 2'000'000;
        {
            Scope child(Stage::Environment);
            Time += 3'000'000;
            FrameProfiler::Count(Counter::SimulationSteps, 2);
        }
        Time += 4'000'000;
    }
    Time += 5'000'000;
    profiler.EndFrame();
    auto samples = profiler.Samples();
    const auto &sample = samples.front();
    Require(sample.Timings[size_t(Stage::GameTick)].InclusiveNs == 9'000'000, "parent inclusive timing");
    Require(sample.Timings[size_t(Stage::GameTick)].SelfNs == 6'000'000, "nested time excluded from self");
    Require(sample.Timings[size_t(Stage::Untracked)].InclusiveNs == 6'000'000, "untracked excludes all root scopes");
    Require(sample.Timings[size_t(Stage::Frame)].InclusiveNs == 15'000'000, "first frame has no startup gap");
    Require(sample.Counters[size_t(Counter::SimulationSteps)] == 2, "counter sum");
    Time += 10'000'000;
    profiler.BeginFrame();
    for (int i = 0; i < 3; ++i) {
        Scope step(Stage::Environment);
        Time += 1'000'000;
    }
    profiler.EndFrame();
    const auto summary = profiler.Summarize();
    Require(summary.Stages[size_t(Stage::Frame)].Milliseconds.Mean == 14, "frame cadence includes preceding gap");
    Require(summary.Stages[size_t(Stage::Environment)].CallsPerFrame == 2, "multiple calls per frame");
    Require(summary.Stages[size_t(Stage::Environment)].MillisecondsPerCall == 1.5, "per-call mean uses total calls");
    Require(summary.Stages[size_t(Stage::GameTick)].Milliseconds.Mean == 4.5, "absent stages count as zero per frame");
    Require(summary.Stages[size_t(Stage::GameTick)].Milliseconds.P95 == 9, "nearest-rank p95");
    profiler.BeginFrame();
    Time += 7'000'000;
    profiler.EndFrame();
    samples = profiler.Samples();
    Require(samples.size() == 2 && samples[0].Sequence == 2 && samples[1].Sequence == 3, "bounded ring keeps chronological order");
    std::ostringstream csv;
    profiler.WriteCsv(csv);
    Require(csv.str().find("world.environment_calls") != std::string::npos, "CSV includes call counts");
    Require(csv.str().find("\n2,") != std::string::npos && csv.str().find("\n3,") != std::string::npos, "CSV contains retained frames");
    profiler.Clear();
    Require(profiler.Size() == 0 && profiler.Summarize().Frames == 0, "clear resets statistics");
}
void CaptureLifetime() {
    FrameProfiler profiler(1, Now), other(1, Now);
    const auto before = ClockReads;
    { Scope disabled(Stage::GameTick); FrameProfiler::Count(Counter::SimulationSteps); }
    Require(ClockReads == before, "disabled scopes must not read the clock");
    profiler.BeginFrame();
    bool rejected = false;
    try { other.BeginFrame(); } catch (const std::logic_error &) { rejected = true; }
    Require(rejected, "overlapping capture rejected");
    try {
        Scope parent(Stage::GameTick);
        Time += 1'000'000;
        throw std::runtime_error("unwind");
    } catch (const std::runtime_error &) {}
    profiler.EndFrame();
    Require(profiler.Samples()[0].Timings[size_t(Stage::GameTick)].Calls == 1, "RAII closes exceptional scope");
    profiler.BeginFrame();
    {
        Scope abandoned(Stage::Environment);
        profiler.CancelFrame();
    }
    Require(profiler.Size() == 1, "cancel does not publish partial frame");
    other.BeginFrame();
    other.EndFrame();
    profiler.Clear();
    profiler.BeginFrame();
    profiler.EndFrame();
    Require(profiler.Samples()[0].Timings[size_t(Stage::Gap)].InclusiveNs == 0, "cancel resets cadence");
}
void MemoryGauges() {
    FrameProfiler profiler(3, Now);
    MemorySnapshot first{{4, 40, 96}, {2, 4, 8}, {}};
    MemorySnapshot second{{4, 4, 6}, {2, 40, 98}, {}};
    profiler.BeginFrame();
    profiler.SetMemoryUsage(second);
    profiler.SetMemoryUsage(first); // Assignment must replace, not accumulate.
    profiler.EndFrame();
    profiler.BeginFrame();
    profiler.SetMemoryUsage(second);
    profiler.EndFrame();
    profiler.BeginFrame(); // Missing sample must not become a zero-memory frame.
    profiler.EndFrame();

    const auto summary = profiler.Summarize();
    const auto &world = summary.Memory[size_t(MemoryComponent::World)];
    Require(world.Samples == 2 && world.Latest == second.World, "latest measured memory and sample count");
    Require(world.MeanUsedBytes == 26 && world.MeanReservedBytes == 55, "memory means ignore missing samples");
    Require(world.PeakReservedBytes == 100, "memory peak uses reserved capacity");
    Require(summary.Memory[size_t(MemoryComponent::Total)].PeakReservedBytes == 110, "total peak is simultaneous, not the sum of component peaks");

    std::ostringstream csv;
    profiler.WriteCsv(csv);
    std::istringstream lines(csv.str());
    std::string header, line;
    std::getline(lines, header);
    const auto columns = std::count(header.begin(), header.end(), ',');
    Require(header.find("memory.world_reserved_bytes") != std::string::npos, "CSV contains memory gauges");
    while (std::getline(lines, line))
        Require(std::count(line.begin(), line.end(), ',') == columns, "missing memory leaves aligned empty CSV fields");
    Require(csv.str().find(",0,,,,,,,,,,,,,,,,,,,,,,,,,\n") != std::string::npos, "CSV distinguishes unmeasured from zero");

    profiler.BeginFrame(); // Ring eviction removes the first world's larger sample.
    profiler.SetMemoryUsage(second);
    profiler.EndFrame();
    Require(profiler.Summarize().Memory[size_t(MemoryComponent::World)].PeakReservedBytes == 10, "rolling peak expires with history");
    profiler.Clear();
    Require(profiler.Summarize().Memory[size_t(MemoryComponent::Total)].Samples == 0, "clear removes memory history");

    profiler.BeginFrame();
    profiler.SetMemoryUsage(first);
    profiler.CancelFrame();
    Require(profiler.Size() == 0, "cancel does not publish memory gauges");
    bool rejected = false;
    try { profiler.SetMemoryUsage(first); } catch (const std::logic_error &) { rejected = true; }
    Require(rejected, "memory writes outside capture rejected");
}

} // namespace
int main() {
    try {
        NestedFrame();
        CaptureLifetime();
        MemoryGauges();
        std::cout << "Performance capture tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
