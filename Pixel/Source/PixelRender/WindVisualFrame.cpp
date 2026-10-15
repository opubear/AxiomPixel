#include "Performance/FrameProfiler.h"
#include "WindVisualFrame.h"
#include "World/World.h"
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
void BuildWindVisualFrame(const pixel::World &world, const WindVisualSettings &settings,
                          float timeSeconds, WorldGridFrame &output) {
    PIXEL_PROFILE_SCOPE(WindVisual);
    const auto state = world.ReadState();
    if (state.Width != Grid2D::Width || state.Height != Grid2D::Height)
        throw std::invalid_argument("wind presentation requires a 1024x1024 world");
    output.Wind = settings;
    output.WindTimeSeconds = timeSeconds;
    output.HasWind = false;
    if (settings.Opacity == 0 || settings.Mode == WindVisualMode::Off || output.DisplayMode != GridDisplayMode::Materials || !world.GetWindEndRow())
        return;
    pixel::performance::FrameProfiler::Count(pixel::performance::Counter::WindVisualCells, state.Cells.size());
    auto texels = output.WindCells();
    for (uint32_t y = 0; y < Grid2D::Height; ++y)
        for (uint32_t x = 0; x < Grid2D::Width; ++x) {
            const size_t i = size_t(y) * Grid2D::Width + x;
            if (state.Cells[i].Type != pixel::CellTypes::Empty) {
                texels[i] = WindSolidTexel;
                continue;
            }
            const auto velocity = world.SampleWind(float(x) + .5f, float(y) + .5f);
            texels[i] = PackWindVelocity(velocity.X, velocity.Y);
            output.HasWind |= texels[i] != 0;
        }
}
