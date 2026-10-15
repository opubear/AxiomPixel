#include "Performance/FrameProfiler.h"
#include "WindSimulation.h"
#include <stdexcept>

namespace pixel {
performance::MemoryUsage WindSimulation::GetMemoryUsage() const {
    auto usage = performance::MeasureObject(*this);
    if (Backend)
        usage.AddOwned(Backend->GetMemoryUsage());

    return usage;
}

void WindSimulation::Configure(WorldStateView world, uint32_t endRow, const WindStrategySettings &settings) {
    auto next = MakeWindStrategy(world, endRow, settings);
    Backend = std::move(next);
}
void WindSimulation::ConfigureCoarse(uint32_t width, uint32_t endRow, const WindSettings &settings) {
    WindStrategySettings configuration;
    configuration.Kind = WindStrategyKind::Coarse;
    configuration.Coarse = settings;
    Configure({width, endRow, {}}, endRow, configuration);
}
void WindSimulation::ConfigureReference(WorldStateView world, uint32_t endRow, const PixelWindSettings &settings) {
    WindStrategySettings configuration;
    configuration.Kind = WindStrategyKind::Reference;
    configuration.Reference = settings;
    Configure(world, endRow, configuration);
}
const WindField &WindSimulation::GetCoarse() const {
    static const WindField disabled;
    const auto *wind = Backend ? Backend->CoarseDiagnostics() : nullptr;
    return wind ? *wind : disabled;
}
const PixelWindField &WindSimulation::GetReference() const {
    static const PixelWindField disabled;
    const auto *wind = Backend ? Backend->ReferenceDiagnostics() : nullptr;
    return wind ? *wind : disabled;
}
void WindSimulation::AdvanceBeforeMaterials(WorldStateView world, float seconds) {
    PIXEL_PROFILE_SCOPE(WindAdvance);
    if (Backend)
        Backend->AdvanceBeforeMaterials(world, seconds);
}
void WindSimulation::UpdateAfterMaterials(WorldStateView world, std::span<const DirtMotion> motion, float seconds) {
    PIXEL_PROFILE_SCOPE(WindFeedback);
    if (Backend)
        Backend->UpdateAfterMaterials(world, motion, seconds);
}
void WindSimulation::AddImpulse(float x, float y, float radius, WindVelocity delta) {
    if (!Backend)
        throw std::invalid_argument("wind impulse requires an active region");
    Backend->AddImpulse(x, y, radius, delta);
}
} // namespace pixel
