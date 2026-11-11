#pragma once
#include "Runtime/Simulation/World/Environment/WorldBiome.h"
#include <array>

namespace pixel {
// Percent suitability in forest/desert/snow order. A mixed region blends the
// probabilities, so populations overlap naturally through an ecotone.
using BiomeSpawnWeights = std::array<uint32_t, 3>;
void ValidateBiomeSpawnWeights(const BiomeSpawnWeights &weights);
bool AcceptBiomeSpawn(const BiomeSample &biome, const BiomeSpawnWeights &weights, uint64_t roll);
uint64_t BiomeSpawnRoll(uint64_t worldSeed, std::string_view species, int64_t anchorX);
inline constexpr BiomeSpawnWeights ResidentBiomeWeights{100, 20, 10};
} // namespace pixel
