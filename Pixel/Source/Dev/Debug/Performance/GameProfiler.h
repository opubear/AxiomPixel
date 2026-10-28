#pragma once
#include "DebugUI/PerformancePanel.h"
#include "FrameProfiler.h"

namespace pixel::performance {
class GameProfiler {
  public:
    std::string ConfigureFromEnvironment(bool perfTest);
    void Attach(axiom::diagnostics::CaptureSession &session);

    bool IsCollecting() const { return Session && Session->ShouldSampleMemory(); }

    void SetMemoryUsage(const MemorySnapshot &memory);
    void DrawUI();

  private:
    axiom::diagnostics::CaptureOptions Options{.Enabled = false, .OutputPath = "pixel-performance.csv"};
    axiom::diagnostics::CaptureSession *Session = nullptr;
    axiom::diagnostics::PerformancePanel Panel;
    std::string Scenario = "default";
};
} // namespace pixel::performance
