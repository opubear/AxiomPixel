#include "PixelRender/PixelWorldPresentation.h"
#include "World/World.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace
void RunPixelWindVisualTests() {
    for (const auto value : {std::array{0.f, 0.f}, std::array{-90.f, 120.f}, std::array{.03125f, -.0625f}, std::array{5000.f, -5000.f}}) {
        const auto packed = PackWindVelocity(value[0], value[1]);
        const auto decoded = UnpackWindVelocity(packed);
        Check(packed != WindSolidTexel, "valid velocity collides with solid sentinel");
        for (size_t axis = 0; axis < 2; ++axis)
            Check(std::abs(decoded[axis] - std::clamp(value[axis], -WindVelocityLimit, WindVelocityLimit)) <= 1.f / 64,
                  "signed wind packing lost direction or precision");
    }
    bool rejected = false;
    try {
        PackWindVelocity(std::numeric_limits<float>::infinity(), 0);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    Check(rejected, "nonfinite wind accepted");
    pixel::World world(Grid2D::Width, Grid2D::Height);
    WorldGridFrame frame;
    WindVisualSettings settings;
    BuildWindVisualFrame(world, settings, .25f, frame);
    Check(!frame.HasWind, "gravity was visualized as wind");
    pixel::WindSettings wind;
    wind.Ambient = {-32, 16};
    wind.GustSpeed = 0;
    world.ConfigureWind(32, wind);
    world.PlaceDirt(2, 2);
    BuildWindVisualFrame(world, settings, .25f, frame);
    auto snapshot = frame.Read();
    Check(snapshot.HasWind && snapshot.WindTimeSeconds == .25f, "wind metadata not published");
    Check(snapshot.WindCells[2 * Grid2D::Width + 2] == WindSolidTexel, "solid lacks wind mask");
    const auto velocity = UnpackWindVelocity(snapshot.WindCells[5 * Grid2D::Width + 5]);
    Check(velocity == std::array{-32.f, 16.f}, "snapshot includes gravity or changes signs");
    Check(snapshot.WindCells[32 * Grid2D::Width] == 0, "wind leaks below region");
    world.ConfigureWindStrategy(32, pixel::WindStrategySettings{});
    BuildWindVisualFrame(world, settings, .5f, frame);
    Check(!frame.HasWind && frame.Read().WindCells[5 * Grid2D::Width + 5] == 0 &&
              frame.Read().WindCells[2 * Grid2D::Width + 2] == WindSolidTexel,
          "empty wind must publish zero velocity and preserve the normal solid mask");
    world.ConfigureWind(32, wind);
    BuildWindVisualFrame(world, settings, .25f, frame);
    world.DisableWind();
    Check(frame.HasWind && frame.Read().WindCells[5 * Grid2D::Width + 5] == PackWindVelocity(-32, 16), "published wind retains mutable world storage");
    BuildWindVisualFrame(world, settings, .5f, frame);
    Check(!frame.HasWind, "disabled wind retains stale enable flag");
    world.ConfigureWind(32, wind);
    settings.Mode = WindVisualMode::Off;
    BuildWindVisualFrame(world, settings, .5f, frame);
    Check(!frame.HasWind, "Off retained wind pass");
    settings.Mode = WindVisualMode::ShortTrails;
    settings.Opacity = 0;
    BuildWindVisualFrame(world, settings, .5f, frame);
    Check(!frame.HasWind, "zero visibility still publishes wind");
    settings.Opacity = .3f;
    frame.DisplayMode = GridDisplayMode::ForceField;
    BuildWindVisualFrame(world, settings, .5f, frame);
    Check(!frame.HasWind, "wind pollutes F1 diagnostic view");
    frame.DisplayMode = GridDisplayMode::Materials;
    world.ConfigureWind(32, pixel::WindSettings{.Ambient = {0, 0}, .GustSpeed = 0});
    BuildWindVisualFrame(world, settings, .5f, frame);
    Check(!frame.HasWind, "calm field generates visible wind");
}
