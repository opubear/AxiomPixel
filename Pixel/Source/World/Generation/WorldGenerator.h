#pragma once

#include "World/WorldMap.h"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace pixel {
enum class WorldGeneratorKind : uint32_t { Flat = 0, Natural = 1 };

struct WorldGenerationSettings {
    WorldGeneratorKind Kind = WorldGeneratorKind::Flat;
    uint32_t Version = 1;
    uint64_t Seed = 1;
    int64_t SurfaceY = 512;
    bool WaterEnabled = true;
    bool operator==(const WorldGenerationSettings &) const = default;
};

void ValidateGenerationSettings(const WorldGenerationSettings &settings);
uint64_t GenerationFingerprint(const WorldGenerationSettings &settings);
const char *GeneratorName(WorldGeneratorKind kind);

class IWorldGenerator {
  public:
    virtual ~IWorldGenerator() = default;
    virtual std::unique_ptr<WorldChunk> GenerateChunk(CellCoord coordinate) const = 0;
    virtual int64_t SurfaceHeight(int64_t x) const = 0;
    virtual uint64_t WorkingBytes() const = 0;
};

std::unique_ptr<IWorldGenerator> CreateWorldGenerator(WorldGenerationSettings settings);

// Pure bounded sampling shares the generator stages and never reads WorldMap.
// The returned vector is row-major. At most 4096 columns and 4M cells per call.
enum class GenerationStage : uint8_t { Strata, Caves, Ores, Water, Surface };
struct WorldGenerationColumn {
    int64_t DrySurfaceY = 0;
    int64_t SurfaceY = 0;
    uint32_t SoilThickness = 0;
    std::optional<int64_t> LakeWaterLevel;
};

WorldGenerationColumn SampleGenerationColumn(WorldGenerationSettings settings, int64_t x);
std::vector<CellTypeId> SampleGeneration(WorldGenerationSettings settings, WorldRect bounds,
                                       GenerationStage through = GenerationStage::Surface);

enum class GenerationWaterKind : uint8_t { SurfaceLake, Groundwater };
struct GenerationWaterBody {
    GenerationWaterKind Kind;
    CellCoord Feature;
    WorldRect Bounds;
    int64_t Level = 0;
    uint32_t CellCount = 0;
};

// Returns complete accepted bodies intersecting bounds, including their total
// volume outside bounds. Diagnostic plans do not create persistent water sources.
std::vector<GenerationWaterBody> PlanGenerationWater(WorldGenerationSettings settings, WorldRect bounds);
} // namespace pixel
