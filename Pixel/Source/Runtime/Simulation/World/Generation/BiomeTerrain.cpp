#include "BiomeTerrain.h"
#include "GenerationNoise.h"
#include <algorithm>
#include <cstdlib>

namespace pixel::generation {
namespace {
int32_t Ramp(int32_t value, int32_t low, int32_t high) {
    return FixedFade(int32_t(std::clamp<int64_t>(int64_t(value - low) * FixedOne / (high - low), 0, FixedOne)));
}

int64_t Blend(int64_t forest, int64_t desert, int64_t snow, const GenerationEnvironment &environment) {
    return FixedRound(forest * environment.ForestWeight + desert * environment.DesertWeight + snow * environment.SnowWeight);
}
} // namespace

BiomeTerrain::BiomeTerrain(uint64_t seed)
    : Temperature(ChannelSeed(seed, "biome-temperature-v2")), Moisture(ChannelSeed(seed, "biome-moisture-v2")),
      Uplift(ChannelSeed(seed, "biome-uplift-v2")), Continental(ChannelSeed(seed, "terrain-continental-v2")),
      Warp(ChannelSeed(seed, "terrain-warp-v2")), Hills(ChannelSeed(seed, "terrain-hills-v2")),
      Detail(ChannelSeed(seed, "terrain-detail-v2")), Dunes(ChannelSeed(seed, "terrain-dunes-v2")),
      Ridges(ChannelSeed(seed, "terrain-ridges-v2")), Soil(ChannelSeed(seed, "terrain-soil-v2")),
      Patches(ChannelSeed(seed, "terrain-strata-v2")), Trees(ChannelSeed(seed, "forest-trees-v2")),
      ForestDensity(ChannelSeed(seed, "forest-density-v2")) {}

GenerationEnvironment BiomeTerrain::Environment(int64_t x) const {
    GenerationEnvironment result;
    // Offset the second axis to avoid placing all 1D climate samples on a
    // Perlin lattice row. Climate is much broader than local landforms.
    result.Temperature = Perlin(Temperature, x, 1733, 4096, 4096);
    result.Moisture = Perlin(Moisture, x, -2917, 4096, 4096);
    result.Uplift = Perlin(Uplift, x, 977, 2048, 2048);
    result.SnowWeight = Ramp(result.Uplift - result.Temperature / 2, 2000, 24000);
    result.DesertWeight = FixedMultiply(FixedOne - result.SnowWeight,
        Ramp(result.Temperature - result.Moisture, 0, 22000));
    result.ForestWeight = FixedOne - result.SnowWeight - result.DesertWeight;
    result.Biome = result.SnowWeight >= result.ForestWeight && result.SnowWeight >= result.DesertWeight
        ? GenerationBiome::SnowMountain
        : result.DesertWeight > result.ForestWeight ? GenerationBiome::Desert : GenerationBiome::Forest;
    return result;
}

int64_t BiomeTerrain::HeightOffset(int64_t x, const GenerationEnvironment &environment) const {
    const int64_t warped = x + FixedRound(96LL * Perlin(Warp, x, 431, 1024, 1024));
    const int64_t continental = FixedRound(96LL * Perlin(Continental, x, 127, 2048, 2048));
    const int64_t hills = FixedRound(42LL * Perlin(Hills, warped, 71, 512, 512));
    const int64_t detail = FixedRound(5LL * Perlin(Detail, x, 31, 64, 64));
    const int64_t dunes = FixedRound(28LL * Perlin(Dunes, warped, 113, 128, 128));
    const int32_t ridge = FixedOne - std::min(FixedOne, 2 * std::abs(Perlin(Ridges, warped, 337, 512, 512)));
    const int64_t peaks = -100 - FixedRound(180LL * FixedMultiply(ridge, ridge));
    return continental + Blend(hills + detail, dunes + detail / 2, peaks + hills + detail, environment);
}

uint32_t BiomeTerrain::SoilThickness(int64_t x, const GenerationEnvironment &environment) const {
    const int64_t variation = FixedRound(8LL * Perlin(Soil, x, 53, 128, 128));
    return uint32_t(std::clamp<int64_t>(Blend(26 + variation, 38 + variation, 6 + variation / 3, environment), 4, 48));
}

CellTypeId BiomeTerrain::Stratum(CellCoord point, int64_t depth, uint32_t soilDepth,
                                const GenerationEnvironment &environment) const {
    if (depth >= 248) return CellTypeId::Stone;
    // The relief is continuously blended; the discrete rock types interleave
    // through a 2D ecotone instead of forming a vertical cut at the winner's X.
    const int32_t patch = Perlin(Patches, point.X, point.Y, 128, 64) / 2;
    const int32_t desert = environment.DesertWeight + patch, snow = environment.SnowWeight - patch;
    const auto biome = snow >= environment.ForestWeight && snow >= desert ? GenerationBiome::SnowMountain
        : desert > environment.ForestWeight ? GenerationBiome::Desert : GenerationBiome::Forest;
    if (depth < soilDepth) {
        if (biome == GenerationBiome::Desert) return CellTypeId::Sand;
        if (biome == GenerationBiome::SnowMountain) return CellTypeId::Snow;
        if (depth > 8 && Perlin(Patches, point.X, point.Y, 128, 32) > 6000) return CellTypeId::Clay;
        return CellTypeId::GroundSoil;
    }
    if (biome == GenerationBiome::Desert && depth < 200 + soilDepth)
        return CellTypeId::Sandstone;
    if (biome == GenerationBiome::SnowMountain && depth < 80 &&
        Perlin(Patches, point.X, point.Y, 64, 32) > 8000)
        return CellTypeId::Ice;
    if (biome == GenerationBiome::Forest && depth < 96 &&
        Perlin(Patches, point.X, point.Y, 64, 64) > -3000)
        return CellTypeId::MossStone;
    return CellTypeId::Stone;
}

bool BiomeTerrain::HasTree(int64_t feature, int64_t &center, uint32_t &height, uint32_t &radius) const {
    const uint64_t hash = FeatureHash(Trees, feature);
    center = feature * 128 + 32 + int64_t(hash % 64);
    const auto environment = Environment(center);
    if (environment.ForestWeight < 42000 ||
        Perlin(ForestDensity, center, 133, 512, 512) < -10000 || (hash >> 8) % 5 == 0)
        return false;
    height = 36 + uint32_t((hash >> 16) % 29);
    radius = 14 + uint32_t((hash >> 24) % 9);
    return true;
}
} // namespace pixel::generation
