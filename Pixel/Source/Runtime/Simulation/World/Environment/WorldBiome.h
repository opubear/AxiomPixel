#pragma once
#include "Runtime/Simulation/World/WorldCoordinates.h"
#include <cstdint>
#include <string_view>

namespace pixel {
struct WorldGenerationSettings;
enum class BiomeId : uint8_t { Forest, Desert, SnowMountain };
std::string_view BiomeKey(BiomeId biome);

// Stable baseline climate, not weather or a measurement of temperature in °C.
// Signed Q16.16 fields; nonnegative weights sum to 65536. Legacy worlds have
// Regional=false and retain their original population and forest presentation.
struct BiomeSample {
    BiomeId Biome = BiomeId::Forest;
    int32_t Temperature = 0, Moisture = 0, Uplift = 0;
    int32_t ForestWeight = 65536, DesertWeight = 0, SnowWeight = 0;
    bool Regional = false;
    int32_t Weight(BiomeId biome) const;
    bool operator==(const BiomeSample &) const = default;
};

// Shared by generation and runtime queries. Surface biomes currently extend
// vertically; edits, residency, camera movement and load order do not affect it.
class BiomeClimate {
  public:
    explicit BiomeClimate(uint64_t seed);
    // Internal sampling may enter generation's reserved feature halo. Public
    // callers validate their requested coordinates before expanding features.
    BiomeSample Sample(int64_t x) const;
  private:
    uint64_t Temperature, Moisture, Uplift;
};
BiomeSample SampleWorldBiome(const WorldGenerationSettings &settings, CellCoord point);
} // namespace pixel
