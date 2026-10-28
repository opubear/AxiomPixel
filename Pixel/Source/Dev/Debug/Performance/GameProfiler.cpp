#include "GameProfiler.h"
#include <cstdlib>
#include <stdexcept>
#ifndef PIXEL_BUILD_TYPE
#define PIXEL_BUILD_TYPE "unknown"
#endif
namespace pixel::performance {
std::string GameProfiler::ConfigureFromEnvironment(bool perfTest) {
    if (!perfTest)
        return Scenario;
    Options.Enabled = true;
    Options = axiom::diagnostics::EnvironmentOptions("PIXEL_PROFILE_", Options);
    if (const auto *scenario = std::getenv("PIXEL_PROFILE_SCENARIO"))
        Scenario = scenario;
    if (Scenario != "default" && Scenario != "no-wind-visual" && Scenario != "paused" && Scenario != "paused-no-wind-visual" && Scenario != "field")
        throw std::invalid_argument("unknown PIXEL_PROFILE_SCENARIO");
    return Scenario;
}

void GameProfiler::Attach(axiom::diagnostics::CaptureSession &session) {
    session.Configure(ProfileSchema(), Options);
    session.SetMetadata(PIXEL_BUILD_TYPE, Scenario);
    Session = &session;
}

void GameProfiler::SetMemoryUsage(const MemorySnapshot &memory) {
    if (!Session)
        throw std::logic_error("Pixel profiling has not been attached");
    for (size_t i = 0; i < MemoryComponentCount; ++i)
        Session->Profiler().SetMemoryUsage({i}, memory.Get(MemoryComponent(i)));
}

void GameProfiler::DrawUI() {
    if (Session)
        Panel.Draw(*Session);
}
} // namespace pixel::performance
