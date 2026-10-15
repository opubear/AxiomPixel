#pragma once
#include "FrameProfiler.h"
#include <memory>
#include <string>

namespace pixel::performance {
// Runtime policy (UI refresh, bounded automatic capture and files) is separate
// from measurement. World/Render only depend on the CPU measurement module.
class GameProfiler {
  public:
    // Called only by the application entry point, never by tests/library users.
    std::string ConfigureFromEnvironment();

    void BeginFrame();

    bool IsCollecting() const { return Collector->IsRecording(); }

    void SetMemoryUsage(const MemorySnapshot &memory) { Collector->SetMemoryUsage(memory); }

    bool EndFrame(); // true after a requested capture was saved successfully

    void DrawUI();

  private:
    std::unique_ptr<FrameProfiler> Collector = std::make_unique<FrameProfiler>();
    bool Enabled = false, ClearRequested = false;
    size_t Warmup = 30, CaptureFrames = 0, Completed = 0;
    std::string Scenario = "default", OutputPath = "pixel-performance.csv", Status;
    Summary Display;
    FrameProfiler::Clock::time_point NextRefresh{};

    void Save(const std::string &path);

    void WriteReport(std::ostream &out) const;

    void DrawMemoryUI() const;
};
} // namespace pixel::performance
