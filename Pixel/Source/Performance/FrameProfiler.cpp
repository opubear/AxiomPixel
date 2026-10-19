#include "FrameProfiler.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <stdexcept>

namespace pixel::performance {
namespace {
constexpr auto StageNames = std::to_array<std::string_view>({
    "frame", "frame.active", "frame.gap", "frame.untracked",
    "publish", "publish.state_copy", "publish.materials", "publish.force_colors", "publish.wind_visual", "publish.validate",
    "game.tick", "world.environment", "wind.advance", "world.material_step", "wind.feedback", "world.character",
    "render.cpu", "render.upload_write", "render.world_commands", "render.wind_commands", "render.character_commands", "render.post_ui_commands", "ui.build"});
static_assert(StageNames.size() == StageCount);
constexpr auto CounterNames = std::to_array<std::string_view>({"simulation_steps", "material_cells_scanned", "active_regions", "wind_visual_cells_scanned", "world_upload_bytes", "wind_upload_bytes",
                                                               "simulation_input_us", "framebuffer_pixels"});
static_assert(CounterNames.size() == CounterCount);
uint64_t Nanoseconds(FrameProfiler::Clock::duration duration) {
    return uint64_t(std::max(int64_t(0), std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()));
}
Distribution Describe(std::vector<double> &values) {
    Distribution result;
    if (values.empty())
        return result;
    for (double value : values)
        result.Mean += value / values.size();
    std::sort(values.begin(), values.end());
    result.P95 = values[size_t(std::ceil(values.size() * .95)) - 1];
    result.Maximum = values.back();
    return result;
}
} // namespace
std::string_view Name(Stage stage) { return StageNames.at(size_t(stage)); }
std::string_view Name(Counter counter) { return CounterNames.at(size_t(counter)); }
thread_local FrameProfiler *FrameProfiler::ActiveProfiler = nullptr;
FrameProfiler::FrameProfiler(size_t capacity, NowFunction now) : History(capacity), Now(now) {
    if (!capacity || !now)
        throw std::invalid_argument("profiler needs storage and a clock");
}
FrameProfiler::~FrameProfiler() { CancelFrame(); }
void FrameProfiler::BeginFrame() {
    if (ActiveProfiler)
        throw std::logic_error("a frame capture is already active on this thread");
    Current = {};
    TopLevelNs = 0;
    Start = Now();
    if (HasLastEnd) {
        const auto gap = Nanoseconds(Start - LastEnd);
        Current.Timings[size_t(Stage::Gap)] = {gap, gap, 1};
    }
    Recording = true;
    ActiveProfiler = this;
}
void FrameProfiler::EndFrame() {
    if (!Recording || ActiveProfiler != this || Top)
        throw std::logic_error("end the frame on its capture thread after closing all scopes");
    const auto end = Now();
    const auto active = Nanoseconds(end - Start);
    const auto frame = active + Current.Timings[size_t(Stage::Gap)].InclusiveNs;
    const auto untracked = active - std::min(active, TopLevelNs);
    Current.Timings[size_t(Stage::Frame)] = {frame, 0, 1};
    Current.Timings[size_t(Stage::Active)] = {active, 0, 1};
    Current.Timings[size_t(Stage::Untracked)] = {untracked, untracked, 1};
    Current.Sequence = ++Sequence;
    History[Next] = Current;
    Next = (Next + 1) % History.size();
    Stored = std::min(Stored + 1, History.size());
    LastEnd = end;
    HasLastEnd = true;
    Recording = false;
    ActiveProfiler = nullptr;
}

void FrameProfiler::SetMemoryUsage(const MemorySnapshot &memory) {
    if (!Recording || ActiveProfiler != this)
        throw std::logic_error("memory samples require an active frame on this thread");

    Current.Memory = memory;
}

void FrameProfiler::CancelFrame() noexcept {
    // Invalidate live scopes as well, so exception/shutdown cleanup cannot leave
    // a dangling capture pointer or publish an incomplete frame.
    while (Top) {
        Top->Owner = nullptr;
        Top = Top->Parent;
    }
    if (ActiveProfiler == this)
        ActiveProfiler = nullptr;
    Recording = false;
    HasLastEnd = false;
}
void FrameProfiler::Clear() {
    if (Recording)
        throw std::logic_error("clear samples between frames");
    Next = Stored = 0;
    Sequence = 0;
}
void FrameProfiler::Count(Counter counter, uint64_t value) noexcept {
    if (ActiveProfiler)
        ActiveProfiler->Current.Counters[size_t(counter)] += value;
}
Scope::Scope(Stage stage) noexcept : Owner(FrameProfiler::ActiveProfiler), Id(stage) {
    if (Owner) {
        Parent = Owner->Top;
        Owner->Top = this;
        Start = Owner->Now();
    }
}
Scope::~Scope() {
    if (!Owner)
        return;
    const auto elapsed = Nanoseconds(Owner->Now() - Start);
    auto &timing = Owner->Current.Timings[size_t(Id)];
    timing.InclusiveNs += elapsed;
    timing.SelfNs += elapsed - std::min(elapsed, ChildrenNs);
    ++timing.Calls;
    Owner->Top = Parent;
    if (Parent)
        Parent->ChildrenNs += elapsed;
    else
        Owner->TopLevelNs += elapsed;
}
std::vector<FrameSample> FrameProfiler::Samples() const {
    std::vector<FrameSample> result;
    result.reserve(Stored);
    for (size_t i = 0; i < Stored; ++i)
        result.push_back(History[(Next + History.size() - Stored + i) % History.size()]);
    return result;
}
Summary FrameProfiler::Summarize() const {
    Summary result;
    const auto samples = Samples();
    result.Frames = samples.size();
    if (samples.empty())
        return result;
    std::vector<double> values;
    values.reserve(samples.size());
    for (size_t stage = 0; stage < StageCount; ++stage) {
        values.clear();
        uint64_t calls = 0, inclusive = 0, self = 0;
        for (const auto &sample : samples) {
            const auto &timing = sample.Timings[stage];
            values.push_back(timing.InclusiveNs / 1e6);
            calls += timing.Calls;
            inclusive += timing.InclusiveNs;
            self += timing.SelfNs;
        }
        result.Stages[stage] = {Describe(values), self / 1e6 / samples.size(), double(calls) / samples.size(),
                               calls ? inclusive / 1e6 / calls : 0};
    }
    for (const auto &sample : samples)
        for (size_t i = 0; i < CounterCount; ++i)
            result.Counters[i] += double(sample.Counters[i]) / samples.size();
    for (const auto &sample : samples) {
        if (!sample.Memory)
            continue;

        for (size_t i = 0; i < MemoryComponentCount; ++i) {
            const auto usage = sample.Memory->Get(MemoryComponent(i));
            auto &memory = result.Memory[i];
            ++memory.Samples;
            memory.Latest = usage;
            memory.MeanUsedBytes += (double(usage.UsedBytes()) - memory.MeanUsedBytes) / memory.Samples;
            memory.MeanReservedBytes += (double(usage.ReservedBytes()) - memory.MeanReservedBytes) / memory.Samples;
            memory.PeakReservedBytes = std::max(memory.PeakReservedBytes, usage.ReservedBytes());
        }
    }

    return result;
}

void FrameProfiler::WriteCsv(std::ostream &out) const {
    out << "frame";
    for (size_t i = 0; i < StageCount; ++i)
        out << ',' << Name(Stage(i)) << "_ms," << Name(Stage(i)) << "_self_ms," << Name(Stage(i)) << "_calls";
    for (size_t i = 0; i < CounterCount; ++i)
        out << ',' << Name(Counter(i));
    out << ",memory_sampled";
    for (size_t i = 0; i < MemoryComponentCount; ++i) {
        const auto name = Name(MemoryComponent(i));
        out << ',' << name << "_object_bytes," << name << "_used_heap_bytes," << name << "_reserved_heap_bytes,"
            << name << "_used_bytes," << name << "_reserved_bytes";
    }

    out << '\n' << std::fixed << std::setprecision(6);
    for (const auto &sample : Samples()) {
        out << sample.Sequence;
        for (const auto &timing : sample.Timings)
            out << ',' << timing.InclusiveNs / 1e6 << ',' << timing.SelfNs / 1e6 << ',' << timing.Calls;
        for (auto value : sample.Counters)
            out << ',' << value;
        out << ',' << int(sample.Memory.has_value());
        for (size_t i = 0; i < MemoryComponentCount; ++i) {
            if (!sample.Memory) {
                out << ",,,,,"; // Unmeasured is not zero usage.
                continue;
            }

            const auto usage = sample.Memory->Get(MemoryComponent(i));
            out << ',' << usage.ObjectBytes << ',' << usage.UsedHeapBytes << ',' << usage.ReservedHeapBytes
                << ',' << usage.UsedBytes() << ',' << usage.ReservedBytes();
        }
        out << '\n';
    }
    if (!out)
        throw std::runtime_error("failed to write performance CSV");
}
} // namespace pixel::performance
