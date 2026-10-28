#pragma once
#include "Core/Diagnostics/EngineMetrics.h"
#include "MemoryUsage.h"

namespace pixel::performance {
enum class Stage : size_t {
    Frame,
    Active,
    Gap,
    Untracked,
    Publication = axiom::diagnostics::EngineStageCount,
    StateCopy,
    Materials,
    ForceColors,
    WindVisual,
    Validation,
    GameTick,
    Environment,
    WindAdvance,
    MaterialStep,
    WindFeedback,
    Character,
    Render,
    Upload,
    WorldCommands,
    WindCommands,
    CharacterCommands,
    PostCommands,
    DebugUI,
    Count
};
enum class Counter : size_t {
    SimulationSteps = axiom::diagnostics::EngineCounterCount,
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
using axiom::diagnostics::Distribution;
using axiom::diagnostics::FrameSample;
using axiom::diagnostics::MemorySummary;
using axiom::diagnostics::StageSummary;
using axiom::diagnostics::Summary;
using axiom::diagnostics::Timing;
const axiom::diagnostics::Schema &ProfileSchema();

// Pixel binds domain names and memory ownership; all sampling is in Axiom.
class FrameProfiler : public axiom::diagnostics::FrameProfiler {
  public:
    explicit FrameProfiler(size_t capacity = 240, NowFunction now = Clock::now)
        : axiom::diagnostics::FrameProfiler(ProfileSchema(), capacity, now) {}

    static void Count(Counter counter, uint64_t value = 1) {
        axiom::diagnostics::FrameProfiler::Count({size_t(counter)}, value);
    }

    void SetMemoryUsage(const MemorySnapshot &memory) {
        for (size_t i = 0; i < MemoryComponentCount; ++i)
            axiom::diagnostics::FrameProfiler::SetMemoryUsage({i}, memory.Get(MemoryComponent(i)));
    }
};

class Scope : public axiom::diagnostics::Scope {
  public:
    explicit Scope(Stage stage) : axiom::diagnostics::Scope({size_t(stage)}) {}
};
} // namespace pixel::performance

#define PIXEL_PROFILE_JOIN_IMPL(a, b) a##b
#define PIXEL_PROFILE_JOIN(a, b) PIXEL_PROFILE_JOIN_IMPL(a, b)
#define PIXEL_PROFILE_SCOPE(stage) ::pixel::performance::Scope PIXEL_PROFILE_JOIN(pixelProfileScope, __LINE__)(::pixel::performance::Stage::stage)
#define PIXEL_PROFILE_BEGIN(instance, stage) ::pixel::performance::Scope instance(::pixel::performance::Stage::stage)
#define PIXEL_PROFILE_END(instance) (instance).End()
