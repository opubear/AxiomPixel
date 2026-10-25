#include "WorldGenerator.h"
#include "GenerationNoise.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <span>
#include <stdexcept>

namespace pixel {
namespace {
using namespace generation;
constexpr int64_t CoordinateLimit = int64_t{1} << 60;
constexpr int64_t CoordinateMargin = 4096;
constexpr int64_t FeatureEdge = 512, FeatureOffset = 256;
constexpr uint32_t WaterWindow = 128;
constexpr size_t WaterWindowCells = size_t(WaterWindow) * WaterWindow;
constexpr int64_t SpawnX = 512, SpawnDryRadius = 144;

void ValidatePoint(CellCoord point) {
    if (point.X <= -CoordinateLimit + CoordinateMargin || point.X >= CoordinateLimit - CoordinateMargin ||
        point.Y <= -CoordinateLimit + CoordinateMargin || point.Y >= CoordinateLimit - CoordinateMargin)
        throw std::out_of_range("generation coordinate exceeds supported range");
}

void ValidateBounds(WorldRect bounds) {
    ValidatePoint({bounds.X, bounds.Y});
    ValidatePoint({bounds.X + bounds.Width, bounds.Y + bounds.Height});
}

WorldRect ChunkBounds(CellCoord coordinate) {
    constexpr int64_t limit = (CoordinateLimit - CoordinateMargin) / storage::ChunkEdge;
    if (coordinate.X <= -limit || coordinate.X >= limit || coordinate.Y <= -limit || coordinate.Y >= limit)
        throw std::out_of_range("generation chunk exceeds supported range");
    const WorldRect bounds{coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge,
                           storage::ChunkEdge, storage::ChunkEdge};
    ValidateBounds(bounds);
    return bounds;
}

int64_t FeatureOf(int64_t coordinate) {
    return FloorDivide(coordinate - FeatureOffset, FeatureEdge);
}

int64_t FeatureOrigin(int64_t feature) {
    return feature * FeatureEdge + FeatureOffset;
}

struct Channels {
    uint64_t Height0, Height1, Height2, Soil, CaveRoom, CaveTunnel, CaveGate;
    uint64_t LakePlacement, LakeWetness, LakeDepth, GroundPlacement, GroundWetness;
    struct Ore { uint64_t Region, Cluster; };
    std::array<Ore, 5> Ores;

    explicit Channels(uint64_t seed)
        : Height0(ChannelSeed(seed, "terrain-height-0-v1")), Height1(ChannelSeed(seed, "terrain-height-1-v1")),
          Height2(ChannelSeed(seed, "terrain-height-2-v1")), Soil(ChannelSeed(seed, "terrain-soil-v1")),
          CaveRoom(ChannelSeed(seed, "cave-room-v1")), CaveTunnel(ChannelSeed(seed, "cave-tunnel-v1")),
          CaveGate(ChannelSeed(seed, "cave-gate-v1")), LakePlacement(ChannelSeed(seed, "lake-placement-v1")),
          LakeWetness(ChannelSeed(seed, "lake-wetness-v1")), LakeDepth(ChannelSeed(seed, "lake-depth-v1")),
          GroundPlacement(ChannelSeed(seed, "groundwater-placement-v1")), GroundWetness(ChannelSeed(seed, "groundwater-wetness-v1")),
          Ores{{{ChannelSeed(seed, "coal-region-v1"), ChannelSeed(seed, "coal-cluster-v1")},
                {ChannelSeed(seed, "copper-region-v1"), ChannelSeed(seed, "copper-cluster-v1")},
                {ChannelSeed(seed, "iron-region-v1"), ChannelSeed(seed, "iron-cluster-v1")},
                {ChannelSeed(seed, "tungsten-region-v1"), ChannelSeed(seed, "tungsten-cluster-v1")},
                {ChannelSeed(seed, "gold-region-v1"), ChannelSeed(seed, "gold-cluster-v1")}}} {}
};

struct LakePlan {
    int64_t Feature = 0, Center = 0, Radius = 0, Level = 0;
    int64_t FirstWater = 0, LastWater = 0, DeepestBed = 0;
    uint32_t TargetDepth = 0, Volume = 0;
};

struct Column {
    int64_t DryHeight = 0, Height = 0, LakeLevel = 0;
    uint32_t SoilDepth = 0;
    bool Wet = false;
};

class NaturalModel {
  public:
    explicit NaturalModel(WorldGenerationSettings settings) : Settings(settings), Noise(settings.Seed), SpawnHeight(RawHeight(SpawnX)) {}

    int64_t DryHeight(int64_t x) const {
        const int64_t distance = x >= SpawnX ? x - SpawnX : SpawnX - x;
        if (distance <= 16)
            return SpawnHeight;
        const int64_t raw = RawHeight(x);
        if (distance >= 80)
            return raw;
        const int32_t weight = FixedFade(int32_t((distance - 16) * FixedOne / 64));
        return SpawnHeight + FixedRound((raw - SpawnHeight) * weight);
    }

    std::vector<LakePlan> LakesFor(int64_t x, uint32_t width) const {
        std::vector<LakePlan> result;
        if (!Settings.WaterEnabled || width == 0)
            return result;
        const int64_t first = FeatureOf(x), last = FeatureOf(x + width - 1);
        result.reserve(size_t(last - first + 1));
        for (int64_t feature = first; feature <= last; ++feature)
            if (auto lake = PlanLake(feature))
                result.push_back(*lake);
        return result;
    }

    Column GetColumn(int64_t x, std::span<const LakePlan> lakes) const {
        Column column;
        column.DryHeight = column.Height = DryHeight(x);
        column.SoilDepth = uint32_t(std::clamp<int64_t>(18 + FixedRound(int64_t(6) * Perlin(Noise.Soil, x, 0, 256, 256)), 12, 24));
        for (const auto &lake : lakes) {
            if (x < lake.Center - lake.Radius || x > lake.Center + lake.Radius)
                continue;
            column.Height = LakeBed(lake, x, column.DryHeight);
            column.LakeLevel = lake.Level;
            column.Wet = column.Height > lake.Level;
            break;
        }
        return column;
    }

    CellTypeId BaseMaterial(CellCoord point, const Column &column, std::span<const LakePlan> lakes, GenerationStage through) const {
        const int64_t depth = point.Y - column.Height;
        if (depth < 0)
            return CellTypeId::Air;
        if (depth < column.SoilDepth)
            return CellTypeId::GroundSoil;
        if (through >= GenerationStage::Caves && IsCave(point, depth, lakes))
            return CellTypeId::Air;
        if (through >= GenerationStage::Ores)
            return OreAt(point, depth);
        return CellTypeId::Stone;
    }

    void Fill(WorldRect bounds, std::span<CellTypeId> output, GenerationStage through) const {
        const auto lakes = LakesFor(bounds.X, bounds.Width);
        std::vector<Column> columns(bounds.Width);
        for (uint32_t x = 0; x < bounds.Width; ++x)
            columns[x] = GetColumn(bounds.X + x, lakes);

        for (uint32_t y = 0; y < bounds.Height; ++y) {
            const int64_t worldY = bounds.Y + y;
            for (uint32_t x = 0; x < bounds.Width; ++x) {
                const auto &column = columns[x];
                auto material = BaseMaterial({bounds.X + x, worldY}, column, lakes, through);
                if (through >= GenerationStage::Water && column.Wet && worldY >= column.LakeLevel && worldY < column.Height)
                    material = CellTypeId::Water;
                output[size_t(y) * bounds.Width + x] = material;
            }
        }

        if (through >= GenerationStage::Water && Settings.WaterEnabled)
            VisitGroundwater(bounds, [&](const GenerationWaterBody &, WorldRect window, std::span<const uint16_t> cells) {
                for (uint16_t index : cells) {
                    const CellCoord point{window.X + index % WaterWindow, window.Y + index / WaterWindow};
                    if (bounds.Contains(point))
                        output[size_t(point.Y - bounds.Y) * bounds.Width + size_t(point.X - bounds.X)] = CellTypeId::Water;
                }
            });

        if (through >= GenerationStage::Surface)
            for (uint32_t x = 0; x < bounds.Width; ++x) {
                const auto &column = columns[x];
                if (column.Wet || column.Height < bounds.Y || column.Height >= bounds.Y + bounds.Height)
                    continue;
                const size_t index = size_t(column.Height - bounds.Y) * bounds.Width + x;
                if (output[index] == CellTypeId::GroundSoil)
                    output[index] = CellTypeId::GrassSoil;
            }
    }

    std::vector<GenerationWaterBody> WaterPlans(WorldRect bounds) const {
        std::vector<GenerationWaterBody> result;
        if (!Settings.WaterEnabled || bounds.Empty())
            return result;
        for (const auto &lake : LakesFor(bounds.X, bounds.Width)) {
            const WorldRect water{lake.FirstWater, lake.Level, uint32_t(lake.LastWater - lake.FirstWater + 1), uint32_t(lake.DeepestBed - lake.Level)};
            if (water.Intersects(bounds))
                result.push_back({GenerationWaterKind::SurfaceLake, {lake.Feature, 0}, water, lake.Level, lake.Volume});
        }
        VisitGroundwater(bounds, [&](const GenerationWaterBody &body, WorldRect, std::span<const uint16_t>) {
            if (body.Bounds.Intersects(bounds))
                result.push_back(body);
        });
        return result;
    }

  private:
    WorldGenerationSettings Settings;
    Channels Noise;
    int64_t SpawnHeight;

    int64_t RawHeight(int64_t x) const {
        return Settings.SurfaceY + FixedRound(int64_t(48) * Perlin(Noise.Height0, x, 0, 1024, 1024) +
                                               int64_t(12) * Perlin(Noise.Height1, x, 0, 256, 256) +
                                               int64_t(4) * Perlin(Noise.Height2, x, 0, 64, 64));
    }

    int64_t LakeBed(const LakePlan &lake, int64_t x, int64_t dry) const {
        const int64_t distance = x >= lake.Center ? x - lake.Center : lake.Center - x;
        const int32_t weight = FixedFade(int32_t((lake.Radius - distance) * FixedOne / lake.Radius));
        const int64_t detail = FixedRound(Perlin(Noise.LakeDepth, x, 0, 64, 64));
        const int64_t depth = std::clamp<int64_t>(int64_t(lake.TargetDepth) + detail, 2, 8);
        const int64_t cut = std::max<int64_t>(0, lake.Level + depth - dry);
        return dry + FixedRound(cut * weight);
    }

    std::optional<LakePlan> PlanLake(int64_t feature) const {
        const uint64_t hash = FeatureHash(Noise.LakePlacement, feature);
        LakePlan lake;
        lake.Feature = feature;
        lake.Center = FeatureOrigin(feature) + 256 + int64_t(hash % 129) - 64;
        lake.Radius = 16 + int64_t((hash >> 8) % 49);
        lake.TargetDepth = 2 + uint32_t((hash >> 16) % 7);
        const int64_t left = lake.Center - lake.Radius, right = lake.Center + lake.Radius;
        if (left - 8 < FeatureOrigin(feature) || right + 8 >= FeatureOrigin(feature) + FeatureEdge ||
            (left - 8 <= SpawnX + SpawnDryRadius && right + 8 >= SpawnX - SpawnDryRadius) ||
            Perlin(Noise.LakeWetness, lake.Center, 0, 2048, 2048) <= -8192)
            return std::nullopt;

        const int64_t leftHeight = DryHeight(left), rightHeight = DryHeight(right);
        if (std::abs(leftHeight - rightHeight) > 8)
            return std::nullopt;
        lake.Level = std::max(leftHeight, rightHeight) + 1;
        lake.DeepestBed = lake.Level;
        int64_t minimum = std::min(leftHeight, rightHeight), maximum = std::max(leftHeight, rightHeight);
        bool sawWater = false, finishedWater = false;
        for (int64_t x = left; x <= right; ++x) {
            const int64_t dry = DryHeight(x), bed = LakeBed(lake, x, dry);
            minimum = std::min(minimum, dry);
            maximum = std::max(maximum, dry);
            if (maximum - minimum > 12 || bed - dry > 24 || bed - lake.Level > 8)
                return std::nullopt;
            if (bed <= lake.Level) {
                finishedWater |= sawWater;
                continue;
            }
            if (finishedWater)
                return std::nullopt;
            if (!sawWater)
                lake.FirstWater = x;
            sawWater = true;
            lake.LastWater = x;
            lake.DeepestBed = std::max(lake.DeepestBed, bed);
            lake.Volume += uint32_t(bed - lake.Level);
        }
        if (!sawWater || lake.LastWater - lake.FirstWater + 1 < 8)
            return std::nullopt;
        return lake;
    }

    bool LakeProtected(CellCoord point, std::span<const LakePlan> lakes) const {
        for (const auto &lake : lakes)
            if (point.X >= lake.FirstWater - 8 && point.X <= lake.LastWater + 8 &&
                point.Y >= lake.Level && point.Y < lake.DeepestBed + 8)
                return true;
        return false;
    }

    bool IsCave(CellCoord point, int64_t depth, std::span<const LakePlan> lakes) const {
        if (depth < 48 || LakeProtected(point, lakes))
            return false;
        const int32_t strength = depth >= 96 ? FixedOne : int32_t((depth - 48) * FixedOne / 48);
        const int32_t roomThreshold = FixedOne - FixedMultiply(FixedOne - 29491, strength);
        if (Perlin(Noise.CaveRoom, point.X, point.Y, 128, 128) > roomThreshold)
            return true;
        const int32_t tunnelWidth = FixedMultiply(3932, strength);
        return std::abs(Perlin(Noise.CaveTunnel, point.X, point.Y, 256, 64)) < tunnelWidth &&
               Perlin(Noise.CaveGate, point.X, point.Y, 512, 512) > 0;
    }

    CellTypeId OreAt(CellCoord point, int64_t depth) const {
        struct OreRule { CellTypeId Material; int64_t MinimumDepth; uint32_t XScale, YScale; int32_t RegionThreshold, ClusterThreshold; };
        // Priority is Gold > Tungsten > Iron > Copper > Coal. These exact Q16
        // thresholds, depths and scales are part of natural/1.
        static constexpr std::array<OreRule, 5> rules{{
            {CellTypeId::CoalOre, 40, 32, 16, -6554, 18350},
            {CellTypeId::CopperOre, 80, 32, 16, 0, 20972},
            {CellTypeId::IronOre, 160, 32, 32, 3277, 22938},
            {CellTypeId::TungstenOre, 384, 16, 32, 6554, 26214},
            {CellTypeId::GoldOre, 320, 16, 16, 9830, 29491}
        }};
        for (size_t i = rules.size(); i-- > 0;) {
            const auto &rule = rules[i];
            if (depth >= rule.MinimumDepth && Perlin(Noise.Ores[i].Region, point.X, point.Y, 512, 512) > rule.RegionThreshold &&
                Perlin(Noise.Ores[i].Cluster, point.X, point.Y, rule.XScale, rule.YScale) > rule.ClusterThreshold)
                return rule.Material;
        }
        return CellTypeId::Stone;
    }

    template <typename Visitor>
    void VisitGroundwater(WorldRect bounds, Visitor &&visitor) const {
        if (bounds.Empty() || !Settings.WaterEnabled)
            return;
        std::array<uint8_t, WaterWindowCells> cells;
        std::array<uint16_t, WaterWindowCells> queue;
        std::array<Column, WaterWindow> columns;
        const int64_t firstX = FeatureOf(bounds.X), lastX = FeatureOf(bounds.X + bounds.Width - 1);
        const int64_t firstY = FeatureOf(bounds.Y), lastY = FeatureOf(bounds.Y + bounds.Height - 1);
        for (int64_t fy = firstY; fy <= lastY; ++fy)
            for (int64_t fx = firstX; fx <= lastX; ++fx) {
                const uint64_t hash = FeatureHash(Noise.GroundPlacement, fx, fy);
                const int64_t centerX = FeatureOrigin(fx) + 256 + int64_t(hash % 129) - 64;
                const WorldRect window{centerX - 64, FeatureOrigin(fy) + 192 + int64_t((hash >> 8) % 129), WaterWindow, WaterWindow};
                if (!window.Intersects(bounds) || Perlin(Noise.GroundWetness, centerX, window.Y, 2048, 2048) <= -8192)
                    continue;
                const auto lakes = LakesFor(window.X, window.Width);
                bool deepEnough = true;
                for (uint32_t x = 0; x < WaterWindow; ++x) {
                    columns[x] = GetColumn(window.X + x, lakes);
                    deepEnough &= window.Y - columns[x].Height >= 96;
                }
                if (!deepEnough)
                    continue;

                // Material classification after caves is equivalent to G4 for
                // connectivity: ores only replace remaining Stone with solids.
                for (uint32_t y = 0; y < WaterWindow; ++y)
                    for (uint32_t x = 0; x < WaterWindow; ++x)
                        cells[size_t(y) * WaterWindow + x] = BaseMaterial({window.X + x, window.Y + y}, columns[x], lakes, GenerationStage::Caves) == CellTypeId::Air;
                size_t seed = WaterWindowCells;
                for (uint32_t y = WaterWindow; y-- > 0;)
                    if (cells[size_t(y) * WaterWindow + WaterWindow / 2]) {
                        seed = size_t(y) * WaterWindow + WaterWindow / 2;
                        break;
                    }
                if (seed == WaterWindowCells)
                    continue;

                size_t head = 0, tail = 1;
                queue[0] = uint16_t(seed);
                cells[seed] = 2;
                bool leaks = false, freeSurface = false;
                uint32_t minX = WaterWindow, maxX = 0, minY = WaterWindow, maxY = 0;
                while (head < tail && !leaks) {
                    const uint32_t index = queue[head++], x = index % WaterWindow, y = index / WaterWindow;
                    if (x == 0 || x == WaterWindow - 1 || y == WaterWindow - 1) {
                        leaks = true;
                        break;
                    }
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                    if (y == 0 && BaseMaterial({window.X + x, window.Y - 1}, columns[x], lakes, GenerationStage::Caves) == CellTypeId::Air)
                        freeSurface = true;
                    const auto add = [&](uint32_t next) {
                        if (cells[next] == 1) {
                            cells[next] = 2;
                            queue[tail++] = uint16_t(next);
                        }
                    };
                    add(index - 1); add(index + 1); add(index + WaterWindow);
                    if (y > 0)
                        add(index - WaterWindow);
                }
                if (leaks || !freeSurface || tail < 32)
                    continue;
                const WorldRect water{window.X + minX, window.Y + minY, maxX - minX + 1, maxY - minY + 1};
                visitor(GenerationWaterBody{GenerationWaterKind::Groundwater, {fx, fy}, water, window.Y, uint32_t(tail)},
                        window, std::span<const uint16_t>(queue.data(), tail));
            }
    }
};

class FlatGenerator final : public IWorldGenerator {
  public:
    explicit FlatGenerator(WorldGenerationSettings settings) : Settings(settings) {}
    std::unique_ptr<WorldChunk> GenerateChunk(CellCoord coordinate) const override {
        const auto bounds = ChunkBounds(coordinate);
        auto chunk = std::make_unique<WorldChunk>(coordinate);
        const auto local = std::clamp<int64_t>(Settings.SurfaceY - bounds.Y, 0, storage::ChunkEdge);
        std::fill(chunk->Data().Materials.begin() + size_t(local) * storage::ChunkEdge, chunk->Data().Materials.end(), CellTypeId::Dirt);
        chunk->Recount();
        chunk->Dirty = false;
        return chunk;
    }
    int64_t SurfaceHeight(int64_t x) const override { ValidatePoint({x, Settings.SurfaceY}); return Settings.SurfaceY; }
    uint64_t WorkingBytes() const override { return 0; }
  private:
    WorldGenerationSettings Settings;
};

class NaturalGenerator final : public IWorldGenerator {
  public:
    explicit NaturalGenerator(WorldGenerationSettings settings) : Model(settings) {}
    std::unique_ptr<WorldChunk> GenerateChunk(CellCoord coordinate) const override {
        const auto bounds = ChunkBounds(coordinate);
        auto chunk = std::make_unique<WorldChunk>(coordinate);
        auto &materials = chunk->Data().Materials;
        Model.Fill(bounds, {materials.data(), materials.size()}, GenerationStage::Surface);
        chunk->Recount();
        chunk->Dirty = false;
        return chunk;
    }
    int64_t SurfaceHeight(int64_t x) const override {
        ValidatePoint({x, 0});
        const auto lakes = Model.LakesFor(x, 1);
        return Model.GetColumn(x, lakes).Height;
    }
    uint64_t WorkingBytes() const override {
        // 48KiB flood-fill arrays + 128 columns + ChunkEdge columns + bounded
        // lake plans, all live simultaneously, are below 128KiB at edge 1024.
        // Reserve a conservative 256KiB, extending columns for larger builds.
        return 256 * 1024 + (storage::ChunkEdge > 1024 ? uint64_t(storage::ChunkEdge - 1024) * (sizeof(Column) + sizeof(LakePlan)) : 0);
    }
  private:
    NaturalModel Model;
};
} // namespace

void ValidateGenerationSettings(const WorldGenerationSettings &settings) {
    if (settings.Kind != WorldGeneratorKind::Flat && settings.Kind != WorldGeneratorKind::Natural)
        throw std::invalid_argument("unknown world generator");
    if (settings.Version != 1)
        throw std::invalid_argument("unsupported world generator version");
    ValidatePoint({0, settings.SurfaceY});
}

const char *GeneratorName(WorldGeneratorKind kind) {
    switch (kind) {
    case WorldGeneratorKind::Flat: return "flat";
    case WorldGeneratorKind::Natural: return "natural";
    }
    throw std::invalid_argument("unknown world generator");
}

uint64_t GenerationFingerprint(const WorldGenerationSettings &settings) {
    ValidateGenerationSettings(settings);
    // Fixed field order, little-endian bytes, FNV-1a. Never hash struct padding.
    uint64_t hash = 14695981039346656037ull;
    const auto append = [&](uint64_t value, uint32_t bytes) {
        for (uint32_t i = 0; i < bytes; ++i) {
            hash = (hash ^ (value & 255)) * 1099511628211ull;
            value >>= 8;
        }
    };
    append(uint32_t(settings.Kind), 4); append(settings.Version, 4);
    append(settings.Seed, 8); append(uint64_t(settings.SurfaceY), 8);
    append(settings.WaterEnabled ? 1 : 0, 1);
    return hash;
}

std::unique_ptr<IWorldGenerator> CreateWorldGenerator(WorldGenerationSettings settings) {
    ValidateGenerationSettings(settings);
    if (settings.Kind == WorldGeneratorKind::Flat)
        return std::make_unique<FlatGenerator>(settings);
    return std::make_unique<NaturalGenerator>(settings);
}

WorldGenerationColumn SampleGenerationColumn(WorldGenerationSettings settings, int64_t x) {
    ValidateGenerationSettings(settings);
    ValidatePoint({x, settings.SurfaceY});
    if (settings.Kind == WorldGeneratorKind::Flat)
        return {settings.SurfaceY, settings.SurfaceY, 0, std::nullopt};
    const NaturalModel model(settings);
    const auto lakes = model.LakesFor(x, 1);
    const auto column = model.GetColumn(x, lakes);
    return {column.DryHeight, column.Height, column.SoilDepth, column.Wet ? std::optional(column.LakeLevel) : std::nullopt};
}

std::vector<CellTypeId> SampleGeneration(WorldGenerationSettings settings, WorldRect bounds, GenerationStage through) {
    ValidateGenerationSettings(settings);
    ValidateBounds(bounds);
    if (through > GenerationStage::Surface || bounds.Width > 4096 || uint64_t(bounds.Width) * bounds.Height > 4 * 1024 * 1024)
        throw std::invalid_argument("generation sample exceeds bounded sampling limits");
    std::vector<CellTypeId> materials(size_t(bounds.Width) * bounds.Height, CellTypeId::Air);
    if (bounds.Empty())
        return materials;
    if (settings.Kind == WorldGeneratorKind::Flat) {
        for (uint32_t y = 0; y < bounds.Height; ++y)
            if (bounds.Y + y >= settings.SurfaceY)
                std::fill_n(materials.begin() + size_t(y) * bounds.Width, bounds.Width, CellTypeId::Dirt);
    } else if (!bounds.Empty()) {
        NaturalModel(settings).Fill(bounds, materials, through);
    }
    return materials;
}

std::vector<GenerationWaterBody> PlanGenerationWater(WorldGenerationSettings settings, WorldRect bounds) {
    ValidateGenerationSettings(settings);
    ValidateBounds(bounds);
    if (bounds.Width > 65536 || bounds.Height > 65536)
        throw std::invalid_argument("water diagnostic range exceeds bounded feature limits");
    if (settings.Kind == WorldGeneratorKind::Flat)
        return {};
    return NaturalModel(settings).WaterPlans(bounds);
}
} // namespace pixel
