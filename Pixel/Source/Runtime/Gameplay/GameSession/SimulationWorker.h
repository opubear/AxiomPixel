#pragma once

#include "Core/Threading/FramePipeline.h"
#include "Dev/Debug/Performance/FrameProfiler.h"
#include <optional>
#include <type_traits>

namespace pixel {
// One bounded simulation job at a time. Host calls are made at joined frame
// boundaries; concurrent game work may use only values captured before Run.
class SimulationWorker {
  public:
    SimulationWorker() : Pipeline([this](float) { Execute(); }) {
        Invoke([this] { Thread = std::this_thread::get_id(); });
    }

    bool IsCurrent() const { return std::this_thread::get_id() == Thread; }
    std::thread::id ThreadId() const { return Thread; }

    template<class F> auto Invoke(F &&function) -> std::invoke_result_t<F> {
        using Result = std::invoke_result_t<F>;
        if (IsCurrent()) return function();
        if constexpr (std::is_void_v<Result>) {
            Run(std::forward<F>(function), [] {}, false);
        } else {
            std::optional<Result> result;
            Run([&] { result.emplace(function()); }, [] {}, false);
            return std::move(*result);
        }
    }

    void Run(std::function<void()> simulation, const std::function<void()> &game, bool profile) {
        Job = std::move(simulation);
        Trace = axiom::diagnostics::TraceWriter::Current();
        Profile = profile;
        try {
            Pipeline.Run(0, game);
        } catch (...) {
            Job = {};
            throw;
        }
        Job = {};
    }

    void MergeMetrics(axiom::diagnostics::FrameProfiler &owner) {
        if (Profile) owner.MergeParallelWork(Metrics);
        Profile = false;
    }

  private:
    void Execute() {
        axiom::diagnostics::TraceThreadScope trace(Trace, 3, "Simulation");
        if (!Profile) { Job(); return; }
        Metrics.BeginParallelWork();
        try {
            PIXEL_PROFILE_SCOPE(SimulationTick);
            Job();
        } catch (...) {
            Metrics.CancelFrame();
            throw;
        }
        Metrics.EndParallelWork();
    }

    std::thread::id Thread;
    std::function<void()> Job;
    axiom::diagnostics::TraceWriter *Trace = nullptr;
    performance::FrameProfiler Metrics{1};
    bool Profile = false;
    // Destroy/join before releasing the callback and its captured state.
    axiom::FramePipeline Pipeline;
};
} // namespace pixel
