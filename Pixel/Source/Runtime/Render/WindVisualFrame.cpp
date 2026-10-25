#include "WindVisualFrame.h"
#include "Dev/Debug/Performance/FrameProfiler.h"
#include "PixelWorldPresentation.h"
#include "Runtime/Simulation/World/World.h"
#include "Runtime/Gameplay/CommittedWorldView.h"
#include "WorldGridFrame.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

uint32_t PackWindVelocity(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y))
        throw std::invalid_argument("wind velocity must be finite");
    const auto pack = [](float value) {
        return uint32_t(uint16_t(int16_t(std::lround(std::clamp(value, -WindVelocityLimit, WindVelocityLimit) * WindVelocityScale))));
    };
    return pack(x) | (pack(y) << 16);
}
std::array<float, 2> UnpackWindVelocity(uint32_t packed) {
    const auto decode = [](uint32_t bits) {
        const int32_t value = int32_t(bits & 0xffff);
        return (value >= 32768 ? value - 65536 : value) / WindVelocityScale;
    };
    return {decode(packed), decode(packed >> 16)};
}
namespace {
pixel::WorldMapView ReadSource(const pixel::World &world, pixel::WorldRect bounds) { return world.GetMap().View(bounds); }
pixel::WorldMapView ReadSource(const pixel::CommittedWorldView &world, pixel::WorldRect) { return world.ReadState(); }
uint64_t WindRevision(const pixel::World &world) { return world.GetEnvironment().GetWindSimulation().GetRevision(); }
uint64_t WindRevision(const pixel::CommittedWorldView &world) { return world.WindRevision(); }
template <class ReadWorld>
void BuildWindVisualFrameImpl(const ReadWorld &world, const WindVisualSettings &settings,
                          float timeSeconds, WorldGridFrame &output) {
    PIXEL_PROFILE_SCOPE(WindVisual);
    output.Wind = settings;
    output.WindTimeSeconds = timeSeconds;
    output.HasWind = false;
    if (settings.Opacity == 0 || settings.Mode == WindVisualMode::Off || output.DisplayMode != GridDisplayMode::Materials || !world.GetWindEndRow())
        return;
    auto texels = output.WindCells();
    const auto origin = output.GetCellOrigin();
    const auto source = ReadSource(world, {origin[0], origin[1], output.GetWidth(), output.GetHeight()});
    const uint64_t revision = WindRevision(world) ^ source.Identity() * 0xd6e8feb86659fd93ULL;
    const bool invalidate = output.PrepareWindRegionVersions(revision);
    for (uint32_t ry = 0; ry < output.RegionRows(); ++ry) {
        for (uint32_t rx = 0; rx < output.RegionColumns(); ++rx) {
            const size_t region = size_t(ry) * output.RegionColumns() + rx;
            const uint32_t beginX = rx * Grid2D::RegionSize, beginY = ry * Grid2D::RegionSize;
            const uint64_t version = GridSourceRegionVersion(source, beginX, beginY,
                                                             std::min(Grid2D::RegionSize, output.GetWidth() - beginX),
                                                             std::min(Grid2D::RegionSize, output.GetHeight() - beginY));
            if (!invalidate && output.WindSourceRegionVersion(region) == version) {
                output.HasWind |= output.WindRegionIsActive(region);
                continue;
            }
            bool changed = false, active = false;
            for (uint32_t y = beginY; y < std::min(output.GetHeight(), beginY + Grid2D::RegionSize); ++y) {
                for (uint32_t x = beginX; x < std::min(output.GetWidth(), beginX + Grid2D::RegionSize); ++x) {
                    const size_t i = size_t(y) * output.GetWidth() + x;
                    const auto cell = world.TryGetCell(origin[0] + x, origin[1] + y);
                    uint32_t packed = WindSolidTexel;
                    if (cell && cell->Type == pixel::CellTypes::Empty) {
                        const auto velocity = world.SampleWind(pixel::CellCoord{origin[0] + x, origin[1] + y});
                        packed = PackWindVelocity(velocity.X, velocity.Y);
                        active |= packed != 0;
                    }
                    changed |= texels[i] != packed;
                    texels[i] = packed;
                    pixel::performance::FrameProfiler::Count(pixel::performance::Counter::WindVisualCells);
                }
            }
            if (changed)
                output.MarkWindRegionChanged(region);
            output.SetWindSourceRegionVersion(region, version);
            output.SetWindRegionActive(region, active);
            output.HasWind |= active;
        }
    }
}
} // namespace
void BuildWindVisualFrame(const pixel::World &world, const WindVisualSettings &settings,
                          float timeSeconds, WorldGridFrame &output) { BuildWindVisualFrameImpl(world, settings, timeSeconds, output); }
void BuildWindVisualFrame(const pixel::CommittedWorldView &world, const WindVisualSettings &settings,
                          float timeSeconds, WorldGridFrame &output) { BuildWindVisualFrameImpl(world, settings, timeSeconds, output); }
