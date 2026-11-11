#include "BiomePopulation.h"
#include "Runtime/Simulation/World/Generation/GenerationNoise.h"
#include <stdexcept>

namespace pixel {
void ValidateBiomeSpawnWeights(const BiomeSpawnWeights &weights) {
    for (const auto weight : weights)
        if (weight > 100) throw std::invalid_argument("biome spawn suitability must be in [0,100]");
}

bool AcceptBiomeSpawn(const BiomeSample &biome, const BiomeSpawnWeights &weights, uint64_t roll) {
    ValidateBiomeSpawnWeights(weights);
    if (!biome.Regional) return true;
    const uint64_t probability = uint64_t(biome.ForestWeight) * weights[0]
        + uint64_t(biome.DesertWeight) * weights[1] + uint64_t(biome.SnowWeight) * weights[2];
    return roll % (100 * uint64_t(generation::FixedOne)) < probability;
}

uint64_t BiomeSpawnRoll(uint64_t worldSeed, std::string_view species, int64_t anchorX) {
    const auto channel = generation::ChannelSeed(generation::ChannelSeed(worldSeed, "biome-population-v1"), species);
    return generation::FeatureHash(channel, anchorX);
}
} // namespace pixel
