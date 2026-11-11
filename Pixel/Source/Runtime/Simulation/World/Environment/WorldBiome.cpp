#include "WorldBiome.h"
#include "Runtime/Simulation/World/Generation/WorldGenerator.h"
#include "Runtime/Simulation/World/Generation/GenerationNoise.h"
#include <algorithm>
#include <stdexcept>

namespace pixel {
using namespace generation;
namespace {
int32_t Ramp(int32_t value, int32_t low, int32_t high) {
    return FixedFade(int32_t(std::clamp<int64_t>(int64_t(value - low) * FixedOne / (high - low), 0, FixedOne)));
}
}

std::string_view BiomeKey(BiomeId biome) {
    switch (biome) {
    case BiomeId::Forest: return "forest";
    case BiomeId::Desert: return "desert";
    case BiomeId::SnowMountain: return "snow_mountain";
    }
    throw std::invalid_argument("unknown biome");
}

int32_t BiomeSample::Weight(BiomeId biome) const {
    switch (biome) {
    case BiomeId::Forest: return ForestWeight;
    case BiomeId::Desert: return DesertWeight;
    case BiomeId::SnowMountain: return SnowWeight;
    }
    throw std::invalid_argument("unknown biome");
}

BiomeClimate::BiomeClimate(uint64_t seed)
    : Temperature(ChannelSeed(seed, "biome-temperature-v2")), Moisture(ChannelSeed(seed, "biome-moisture-v2")),
      Uplift(ChannelSeed(seed, "biome-uplift-v2")) {}

BiomeSample BiomeClimate::Sample(int64_t x) const {
    BiomeSample result;
    result.Regional = true;
    // Preserve Natural v2 channels and fixed-point arithmetic exactly.
    result.Temperature = Perlin(Temperature, x, 1733, 4096, 4096);
    result.Moisture = Perlin(Moisture, x, -2917, 4096, 4096);
    result.Uplift = Perlin(Uplift, x, 977, 2048, 2048);
    result.SnowWeight = Ramp(result.Uplift - result.Temperature / 2, 2000, 24000);
    result.DesertWeight = FixedMultiply(FixedOne - result.SnowWeight,
        Ramp(result.Temperature - result.Moisture, 0, 22000));
    result.ForestWeight = FixedOne - result.SnowWeight - result.DesertWeight;
    result.Biome = result.SnowWeight >= result.ForestWeight && result.SnowWeight >= result.DesertWeight
        ? BiomeId::SnowMountain : result.DesertWeight > result.ForestWeight ? BiomeId::Desert : BiomeId::Forest;
    return result;
}

BiomeSample SampleWorldBiome(const WorldGenerationSettings &settings, CellCoord point) {
    ValidateGenerationSettings(settings);
    ValidateGenerationPoint(point);
    if (settings.Kind == WorldGeneratorKind::Natural && settings.Version == 2)
        return BiomeClimate(settings.Seed).Sample(point.X);
    return {};
}
} // namespace pixel
