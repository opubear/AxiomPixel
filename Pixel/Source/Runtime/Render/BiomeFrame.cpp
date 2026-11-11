#include "BiomeFrame.h"
#include "Runtime/Simulation/World/Generation/GenerationNoise.h"

namespace pixel {
namespace {
bool SupportsDecoration(const CommittedWorldView &world, CellCoord ground) {
    for (int dx = -1; dx <= 1; ++dx) {
        const auto support = world.TryGetCell(ground.X + dx, ground.Y);
        if (!support || !BlocksCharacter(support->Type)) return false;
        for (int dy = -4; dy < 0; ++dy) {
            const auto cell = world.TryGetCell(ground.X + dx, ground.Y + dy);
            if (!cell || cell->Type != CellTypeId::Air) return false;
        }
    }
    return true;
}
} // namespace

void BuildBiomeFrame(const CommittedWorldView &world, const Camera2D &camera,
                     std::array<int, 2> framebufferSize, WorldGridFrame &output) {
    output.BiomeWeights = {1, 0, 0};
    output.DecorationDraws.clear();
    const auto reference = camera.GetWorldPosition().Cell();
    const auto climate = world.SampleBiome(reference);
    if (!climate.Regional || output.DisplayMode != GridDisplayMode::Materials) return;
    output.BiomeWeights = {climate.ForestWeight / 65536.f, climate.DesertWeight / 65536.f, climate.SnowWeight / 65536.f};
    if (framebufferSize[0] <= 0 || framebufferSize[1] <= 0) return;

    const auto bounds = world.Bounds();
    const auto centre = camera.RelativeCellsTo(reference);
    const auto channel = generation::ChannelSeed(world.GenerationSettings().Seed, "biome-decoration-v1");
    output.DecorationDraws.reserve(MaximumBiomeDecorationParts);
    for (int64_t feature = FloorDivide(bounds.X, 64); feature <= FloorDivide(bounds.X + bounds.Width - 1, 64); ++feature) {
        const auto hash = generation::FeatureHash(channel, feature);
        const auto x = feature * 64 + 16 + int64_t(hash % 32);
        if (x >= 480 && x <= 544) continue; // keep the initial teaching area clear
        const auto column = SampleGenerationColumn(world.GenerationSettings(), x);
        if (!SupportsDecoration(world, {x, column.SurfaceY})) continue;
        const double left = (double(x - reference.X) + .5 - centre[0]) * output.View.CellSizePixels[0] + framebufferSize[0] / 2.0;
        const double ground = (double(column.SurfaceY - reference.Y) - centre[1]) * output.View.CellSizePixels[1] + framebufferSize[1] / 2.0;
        const auto part = [&](double dx, double dy, double width, double height, std::array<float, 4> color) {
            const double rx = width * .5 * output.View.CellSizePixels[0], ry = height * .5 * output.View.CellSizePixels[1];
            const double px = left + dx * output.View.CellSizePixels[0], py = ground + dy * output.View.CellSizePixels[1];
            if (px + rx < 0 || py + ry < 0 || px - rx > framebufferSize[0] || py - ry > framebufferSize[1]) return;
            if (output.DecorationDraws.size() == MaximumBiomeDecorationParts)
                throw std::logic_error("biome decoration publication exceeded its bound");
            output.DecorationDraws.push_back({{float(px), float(py), float(rx), float(ry)}, color});
        };
        switch (column.Environment.Biome) {
        case BiomeId::Forest:
            part(0, -1, .35, 2, {.14f, .32f, .08f, 1});
            part(-.5, -.8, .8, .35, {.24f, .45f, .12f, 1});
            part(.4, -1.4, .8, .35, {.24f, .45f, .12f, 1});
            part(0, -2, .65, .5, {.85f, .55f, .20f, 1});
            break;
        case BiomeId::Desert:
            part(0, -.9, .35, 1.8, {.38f, .24f, .10f, 1});
            part(0, -.7, 2, .3, {.48f, .32f, .15f, 1});
            part(-.8, -1.1, .3, 1, {.48f, .32f, .15f, 1});
            break;
        case BiomeId::SnowMountain:
            part(0, -.45, 2.4, .9, {.32f, .42f, .52f, 1});
            part(-.2, -.95, 1.6, .45, {.79f, .88f, .95f, 1});
            break;
        }
    }
}
} // namespace pixel
