#pragma once

#include "MemoryUsage.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string_view>
#include <vector>

namespace pixel::performance {
// Stable identifiers keep the hot path free of strings, allocations and locks.
// Add a name in FrameProfiler.cpp when adding a measurement here.
enum class Stage : size_t {
    Frame, Active, Gap, Untracked,
    Publication, StateCopy, Materials, ForceColors, WindVisual, Validation,
    GameTick, Environment, WindAdvance, MaterialStep, WindFeedback, Character,
    Render, Upload, WorldCommands, WindCommands, CharacterCommands, PostCommands, DebugUI,
    Count
};
enum class Counter : size_t {
    SimulationSteps,
    MaterialCells,
    ActiveRegions,
    WindVisualCells,
    WorldUploadBytes,
    WindUploadBytes,
    SimulationInputMicroseconds,
    FramebufferPixels,
    Count
};
constexpr size_t StageCount = size_t(Stage::Count);
constexpr size_t CounterCount = size_t(Counter::Count);

std::string_view Name(Stage stage);

std::string_view Name(Counter counter);
struct Timing {
    uint64_t InclusiveNs = 0, SelfNs = 0, Calls = 0;
};
struct FrameSample {
    uint64_t Sequence = 0;
    std::array<Timing, StageCount> Timings{};
    std::array<uint64_t, CounterCount> Counters{};
    std::optional<MemorySnapshot> Memory;
};
struct Distribution {
    double Mean = 0, P95 = 0, Maximum = 0;
};
struct StageSummary {
    Distribution Milliseconds;
    double SelfMilliseconds = 0, CallsPerFrame = 0, MillisecondsPerCall = 0;
};
struct MemorySummary {
    size_t Samples = 0;
    MemoryUsage Latest;
    double MeanUsedBytes = 0;
    double MeanReservedBytes = 0;
    uint64_t PeakReservedBytes = 0; // Maximum frame-end sample in retained history.
};

struct Summary {
    size_t Frames = 0;
    std::array<StageSummary, StageCount> Stages{};
    std::array<double, CounterCount> Counters{};
    std::array<MemorySummary, MemoryComponentCount> Memory{};
};
class Scope;
// One capture per thread; scopes and Begin/EndFrame must stay on that thread.
// No engine, rendering, logging or UI dependencies. Disabled scopes do not read
// the clock. Storage is bounded and allocated once, before any measured frame.
class FrameProfiler {
  public:
    using Clock = std::chrono::steady_clock;
    using NowFunction = Clock::time_point (*)();

    explicit FrameProfiler(size_t capacity = 240, NowFunction now = Clock::now);

    ~FrameProfiler();

    FrameProfiler(const FrameProfiler &) = delete;

    FrameProfiler &operator=(const FrameProfiler &) = delete;

    void BeginFrame();

    void EndFrame();

    // Gauge assignment, not accumulation. Replaces this frame's earlier value.
    void SetMemoryUsage(const MemorySnapshot &memory);

    void CancelFrame() noexcept;

    void Clear(); // Retains frame cadence, discards stored samples.

    std::vector<FrameSample> Samples() const; // Oldest to newest; outside hot path.

    Summary Summarize() const;

    void WriteCsv(std::ostream &out) const;

    size_t Size() const { return Stored; }

    bool IsRecording() const { return Recording; }

    static void Count(Counter counter, uint64_t value = 1) noexcept;

  private:
    friend class Scope;
    static thread_local FrameProfiler *ActiveProfiler;
    std::vector<FrameSample> History;
    size_t Next = 0, Stored = 0;
    uint64_t Sequence = 0, TopLevelNs = 0;
    FrameSample Current;
    NowFunction Now;
    Clock::time_point Start{}, LastEnd{};
    bool Recording = false, HasLastEnd = false;
    Scope *Top = nullptr;
};
class Scope {
  public:
    explicit Scope(Stage stage) noexcept;

    ~Scope();

    Scope(const Scope &) = delete;

    Scope &operator=(const Scope &) = delete;
  private:
    friend class FrameProfiler;
    FrameProfiler *Owner;
    Scope *Parent = nullptr;
    Stage Id;
    FrameProfiler::Clock::time_point Start{};
    uint64_t ChildrenNs = 0;
};
} // namespace pixel::performance

#define PIXEL_PROFILE_JOIN_IMPL(a, b) a##b
#define PIXEL_PROFILE_JOIN(a, b) PIXEL_PROFILE_JOIN_IMPL(a, b)
#define PIXEL_PROFILE_SCOPE(stage) ::pixel::performance::Scope PIXEL_PROFILE_JOIN(pixelProfileScope, __LINE__)(::pixel::performance::Stage::stage)
