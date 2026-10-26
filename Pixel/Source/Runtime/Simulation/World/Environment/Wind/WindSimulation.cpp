#include "WindSimulation.h"
#include "Dev/Debug/Performance/FrameProfiler.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixel {
void WindSimulation::ShareFrom(const WindSimulation &source) {
    Backend = source.Backend;
    Settings = source.Settings;
    Domain = source.Domain;
    ConfiguredEndRow = source.ConfiguredEndRow;
    Configured = source.Configured;
    FollowsView = source.FollowsView;
    SimulationTime = source.SimulationTime;
    Revision = source.Revision;
}

void WindSimulation::PrepareWrite() {
    if (Backend && Backend.use_count() != 1) {
        Backend = Backend->Clone();
    }
}
performance::MemoryUsage WindSimulation::GetMemoryUsage() const {
    auto usage = performance::MeasureObject(*this);
    if (Backend)
        usage.AddOwned(Backend->GetMemoryUsage());
    return usage;
}
void WindSimulation::Configure(WorldMapView world, uint32_t endRow, const WindStrategySettings &settings) {
    if (endRow > world.Height)
        throw std::out_of_range("wind boundary is outside the supplied domain");
    if (settings.Kind < WindStrategyKind::Coarse || settings.Kind > WindStrategyKind::Empty)
        throw std::invalid_argument("unknown wind strategy");
    auto next = endRow ? MakeWindStrategy(world, endRow, settings) : nullptr;
    Settings = settings;
    Configured = true;
    ConfiguredEndRow = endRow;
    FollowsView = endRow == world.Height || endRow == 0;
    Domain = {world.Origin, world.Width, endRow};
    SimulationTime = 0;
    Backend = std::move(next);
    ++Revision;
}
void WindSimulation::Clear() {
    if (Configured || Backend)
        ++Revision;
    Backend.reset();
    Configured = false;
    Domain = {};
    SimulationTime = 0;
}
void WindSimulation::MoveDomain(WorldMapView world, uint32_t endRow) {
    if (!Configured)
        return;
    if (endRow > world.Height)
        throw std::out_of_range("wind boundary is outside the supplied domain");
    const WindDomain nextDomain{world.Origin, world.Width, endRow};
    if (nextDomain == Domain)
        return;
    if (!world.Width || !endRow) {
        Backend.reset();
        Domain = nextDomain;
        ++Revision;
        return;
    }
    const int64_t dx = WindCoordinateOffset(world.Origin.X, Domain.Origin.X);
    const int64_t dy = WindCoordinateOffset(world.Origin.Y, Domain.Origin.Y);
    const bool overlaps = dx < int64_t(Domain.Width) && dy < int64_t(Domain.Height)
                       && dx > -int64_t(world.Width) && dy > -int64_t(endRow);
    if (Backend && overlaps) {
        PrepareWrite();
        Backend->MoveDomain(world, endRow, dx, dy);
    } else {
        auto next = MakeWindStrategy(world, endRow, Settings);
        next->SetSimulationTime(SimulationTime);
        Backend = std::move(next);
    }
    Domain = nextDomain;
    ++Revision;
}
void WindSimulation::ConfigureCoarse(uint32_t width, uint32_t endRow, const WindSettings &settings) {
    WindStrategySettings configuration;
    configuration.Kind = WindStrategyKind::Coarse;
    configuration.Coarse = settings;
    Configure({width, endRow, {}}, endRow, configuration);
}
void WindSimulation::ConfigureReference(WorldMapView world, uint32_t endRow, const PixelWindSettings &settings) {
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
void WindSimulation::AdvanceBeforeMaterials(WorldMapView world, float seconds) {
    PIXEL_PROFILE_SCOPE(WindAdvance);
    MoveDomain(world, FollowsView ? world.Height : std::min(world.Height, ConfiguredEndRow));
    if (Backend) {
        PrepareWrite();
        Backend->AdvanceBeforeMaterials(world, seconds);
        if (Backend->Kind() != WindStrategyKind::Empty && std::isfinite(seconds) && seconds > 0)
            ++Revision;
    }
    if (Configured && std::isfinite(seconds) && seconds > 0)
        SimulationTime += std::min(double(seconds), .25);
}
void WindSimulation::UpdateAfterMaterials(WorldMapView world, std::span<const DirtMotion> motion, float seconds) {
    PIXEL_PROFILE_SCOPE(WindFeedback);
    if (Backend) {
        PrepareWrite();
        Backend->UpdateAfterMaterials(world, motion, seconds);
        if (Backend->Kind() != WindStrategyKind::Empty && std::isfinite(seconds) && seconds > 0)
            ++Revision;
    }
}
void WindSimulation::AddImpulse(float x, float y, float radius, WindVelocity delta) {
    if (!Backend)
        throw std::invalid_argument("wind impulse requires an active domain");
    PrepareWrite();
    Backend->AddImpulse(x, y, radius, delta);
    if (Backend->Kind() != WindStrategyKind::Empty && (delta.X != 0 || delta.Y != 0))
        ++Revision;
}
void WindSimulation::SetSimulationTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("wind simulation time must be finite and nonnegative");
    if (SimulationTime == seconds)
        return;
    if (Backend && GetKind() == WindStrategyKind::Procedural) {
        PrepareWrite();
        Backend->SetSimulationTime(seconds);
    }
    SimulationTime = seconds;
    if (GetKind() == WindStrategyKind::Procedural)
        ++Revision;
}
void WindSimulation::AddWorldImpulse(double x, double y, float radius, WindVelocity delta) {
    const double localX = x - double(Domain.Origin.X), localY = y - double(Domain.Origin.Y);
    if (!std::isfinite(localX) || !std::isfinite(localY) || localX < 0 || localY < 0 || localX >= Domain.Width || localY >= Domain.Height)
        throw std::invalid_argument("wind impulse lies outside the active domain");
    AddImpulse(float(localX), float(localY), radius, delta);
}
bool WindSimulation::AddWorldImpulse(WorldPosition position, float radius, WindVelocity delta) {
    if (!Backend || !Domain.Width || !Domain.Height || !std::isfinite(radius) || radius <= 0
        || !std::isfinite(delta.X) || !std::isfinite(delta.Y))
        return false;
    position.Normalize();
    const auto local = position.RelativeTo(Domain.Origin);
    if (local[0] < 0 || local[1] < 0 || local[0] >= Domain.Width || local[1] >= Domain.Height)
        return false;
    const auto narrowInside = [](double value, uint32_t extent) {
        return std::clamp(float(value), 0.0f, std::nextafter(float(extent), 0.0f));
    };
    AddImpulse(narrowInside(local[0], Domain.Width), narrowInside(local[1], Domain.Height), radius, delta);
    return true;
}
WindVelocity WindSimulation::SampleWorld(double x, double y) const {
    return Sample(float(x - double(Domain.Origin.X)), float(y - double(Domain.Origin.Y)));
}
WindVelocity WindSimulation::SampleWorld(CellCoord cell, float offsetX, float offsetY) const {
    const int64_t x = WindCoordinateOffset(cell.X, Domain.Origin.X);
    const int64_t y = WindCoordinateOffset(cell.Y, Domain.Origin.Y);
    return Sample(float(x) + offsetX, float(y) + offsetY);
}
WindVelocity WindSimulation::SampleForMaterial(CellCoord position) const {
    const int64_t x = WindCoordinateOffset(position.X, Domain.Origin.X);
    const int64_t y = WindCoordinateOffset(position.Y, Domain.Origin.Y);
    if (x < 0 || y < 0 || x >= int64_t(Domain.Width) || y >= int64_t(Domain.Height))
        return {};
    return SampleForMaterial(uint32_t(x), uint32_t(y));
}
} // namespace pixel
