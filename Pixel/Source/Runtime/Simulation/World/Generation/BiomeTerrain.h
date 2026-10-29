#pragma once

#include "WorldGenerator.h"

namespace pixel::generation {
// Natural v2's environment and terrain rules. All fields are world-coordinate
// samples; no neighbouring chunks or mutable random streams are consulted.
class BiomeTerrain {
  public:
    explicit BiomeTerrain(uint64_t seed);

    GenerationEnvironment Environment(int64_t x) const;
    int64_t HeightOffset(int64_t x, const GenerationEnvironment &environment) const;
    uint32_t SoilThickness(int64_t x, const GenerationEnvironment &environment) const;
    CellTypeId Stratum(CellCoord point, int64_t depth, uint32_t soilDepth,
                       const GenerationEnvironment &environment) const;
    bool HasTree(int64_t feature, int64_t &center, uint32_t &height, uint32_t &radius) const;

  private:
    uint64_t Temperature, Moisture, Uplift, Continental, Warp, Hills, Detail;
    uint64_t Dunes, Ridges, Soil, Patches, Trees, ForestDensity;
};
} // namespace pixel::generation
