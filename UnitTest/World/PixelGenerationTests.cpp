#include "Runtime/Simulation/World/Generation/GenerationNoise.h"
#include "Runtime/Simulation/World/Generation/WorldGenerator.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

namespace {
using namespace pixel;

void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Function>
void RequireRejected(Function &&function, const char *message) {
    bool rejected = false;
    try { function(); } catch (const std::exception &) { rejected = true; }
    Require(rejected, message);
}

WorldGenerationSettings Natural(uint64_t seed = 1) {
    return {WorldGeneratorKind::Natural, 1, seed};
}

uint64_t MaterialDigest(std::span<const CellTypeId> materials) {
    uint64_t digest = 14695981039346656037ull;
    for (CellTypeId material : materials) {
        uint32_t value = uint32_t(material);
        for (uint32_t byte = 0; byte < 4; ++byte) {
            digest = (digest ^ (value & 255)) * 1099511628211ull;
            value >>= 8;
        }
    }
    return digest;
}

void NoiseContractAndSettings() {
    using namespace generation;
    const uint64_t channel = ChannelSeed(1, "terrain-height-0-v1");
    Require(channel == 6628844354781172079ull, "noise channel derivation changed");
    Require(Perlin(channel, 0, 0, 1024, 1024) == -32 && Perlin(channel, -1, -1, 1024, 1024) == 32,
            "cell-centre or negative-lattice golden vectors changed");
    Require(Perlin(channel, 12345, -23456, 256, 64) == 13714 &&
                Perlin(channel, (int64_t{1} << 55) + 17, -(int64_t{1} << 55) + 29, 128, 128) == -2820,
            "fixed interpolation or distant-coordinate golden vectors changed");
    Require(FixedMultiply(-1, FixedOne / 2) == -1 && FixedRound(-FixedOne / 2) == 0,
            "negative products must floor and half rounding must floor(value+0.5)");
    RequireRejected([&] { Perlin(channel, 0, 0, 7, 64); }, "invalid noise scale was accepted");
    RequireRejected([&] { Perlin(channel, 0, 0, 32768, 64); }, "oversized noise scale was accepted");

    const auto settings = Natural();
    Require(GenerationFingerprint(settings) == 3760764308338698577ull, "normalized generation fingerprint changed");
    auto changed = settings;
    changed.Seed = 2;
    Require(GenerationFingerprint(changed) != GenerationFingerprint(settings), "seed is absent from generation identity");
    changed = settings;
    changed.WaterEnabled = false;
    Require(GenerationFingerprint(changed) != GenerationFingerprint(settings), "water switch is absent from generation identity");
    changed = settings; changed.Version = 2;
    RequireRejected([&] { CreateWorldGenerator(changed); }, "unknown natural version was accepted");
    changed = settings; changed.Kind = WorldGeneratorKind(99);
    RequireRejected([&] { CreateWorldGenerator(changed); }, "unknown generator was accepted");
    changed = settings; changed.SurfaceY = int64_t{1} << 60;
    RequireRejected([&] { CreateWorldGenerator(changed); }, "unsupported surface coordinate was accepted");
    RequireRejected([&] { SampleGeneration(settings, {std::numeric_limits<int64_t>::max(), 0, 1, 1}); },
                    "coordinate overflow input was accepted");
    RequireRejected([&] { CreateWorldGenerator(settings)->GenerateChunk({std::numeric_limits<int64_t>::max(), 0}); },
                    "chunk multiplication overflow input was accepted");
}

void FlatCompatibilityAndPayload() {
    WorldGenerationSettings settings;
    auto generator = CreateWorldGenerator(settings);
    Require(generator->WorkingBytes() == 0 && generator->SurfaceHeight(-9999) == 512, "flat generator contract changed");
    for (int64_t chunkY : {FloorDivide(511, storage::ChunkEdge), FloorDivide(512, storage::ChunkEdge)}) {
        auto chunk = generator->GenerateChunk({-1, chunkY});
        uint64_t solids = 0;
        for (uint32_t y = 0; y < storage::ChunkEdge; ++y)
            for (uint32_t x = 0; x < storage::ChunkEdge; ++x) {
                const size_t index = size_t(y) * storage::ChunkEdge + x;
                const bool underground = chunkY * storage::ChunkEdge + y >= 512;
                Require(chunk->Data().Materials[index] == (underground ? CellTypeId::Dirt : CellTypeId::Air),
                        "flat terrain must remain exactly y<512 Air, y>=512 Dirt");
                Require(chunk->Data().Directions[index].X == 0 && chunk->Data().Directions[index].Y == 1 &&
                            chunk->Data().BaseForces[index] == ForceVector{0, 3600},
                        "generation changed spatial direction or base force");
                solids += underground;
            }
        Require(!chunk->Dirty && std::accumulate(chunk->MaterialCounts.begin(), chunk->MaterialCounts.end(), uint64_t{0}) == solids,
                "new flat chunk is dirty or has incorrect region counts");
    }
    Require(PlanGenerationWater(settings, {-1024, 0, 2048, 2048}).empty(), "flat compatibility world acquired water");
    settings.SurfaceY = -17;
    const auto translated = SampleGeneration(settings, {-3, -19, 5, 5});
    for (uint32_t y = 0; y < 5; ++y)
        Require(translated[size_t(y) * 5] == (y < 2 ? CellTypeId::Air : CellTypeId::Dirt), "custom flat height was ignored");
}

void CompareTiling(WorldGenerationSettings settings, WorldRect bounds) {
    const auto whole = SampleGeneration(settings, bounds);
    for (uint32_t y = 0; y < bounds.Height; y += 23)
        for (uint32_t x = 0; x < bounds.Width; x += 17) {
            const WorldRect tile{bounds.X + x, bounds.Y + y, std::min<uint32_t>(17, bounds.Width - x), std::min<uint32_t>(23, bounds.Height - y)};
            const auto part = SampleGeneration(settings, tile);
            for (uint32_t dy = 0; dy < tile.Height; ++dy)
                for (uint32_t dx = 0; dx < tile.Width; ++dx)
                    Require(part[size_t(dy) * tile.Width + dx] == whole[size_t(y + dy) * bounds.Width + x + dx],
                            "material depends on internal tile bounds or generation order");
        }
}

void CoordinatesTilingAndChunkPublication() {
    const auto settings = Natural(42);
    CompareTiling(settings, {-37, 480, 96, 128});
    CompareTiling(settings, {(int64_t{1} << 55) - 31, 480, 64, 96});
    CompareTiling(settings, {-(int64_t{1} << 55) - 17, (int64_t{1} << 55) + 71, 48, 64});
    for (int64_t x : {-(int64_t{1} << 55), int64_t{-1}, int64_t{0}, int64_t{1} << 55}) {
        const auto column = SampleGenerationColumn(settings, x);
        Require(column.DrySurfaceY >= 448 && column.DrySurfaceY <= 576 && column.SurfaceY <= 600 &&
                    column.SoilThickness >= 12 && column.SoilThickness <= 24,
                "large or negative coordinate exceeded fixed terrain bounds");
    }

    auto generator = CreateWorldGenerator(settings);
    Require(generator->WorkingBytes() <= 256 * 1024 || storage::ChunkEdge > 1024, "default natural scratch reservation exceeds 256KiB");
    const int64_t chunkY = FloorDivide(512, storage::ChunkEdge);
    auto right = generator->GenerateChunk({0, chunkY});
    const uint64_t digest = MaterialDigest({right->Data().Materials.data(), right->Data().Materials.size()});
    auto left = generator->GenerateChunk({-1, chunkY});
    const auto sample = SampleGeneration(settings, {-8, 512, 16, 64});
    for (uint32_t y = 0; y < 64; ++y)
        for (int64_t x = -8; x < 8; ++x) {
            const auto &chunk = x < 0 ? *left : *right;
            const uint32_t localX = uint32_t(x - chunk.Coordinate.X * storage::ChunkEdge);
            const uint32_t localY = uint32_t(512 + y - chunkY * storage::ChunkEdge);
            Require(chunk.Data().Materials[size_t(localY) * storage::ChunkEdge + localX] == sample[size_t(y) * 16 + size_t(x + 8)],
                    "actual adjacent chunks disagree with pure world-coordinate sampling");
        }
    right.reset();
    right = generator->GenerateChunk({0, chunkY});
    Require(MaterialDigest({right->Data().Materials.data(), right->Data().Materials.size()}) == digest && !right->Dirty,
            "chunk regeneration after reverse-order work changed its base materials");
    for (size_t i = 0; i < right->Data().Materials.size(); ++i)
        Require(right->Data().Directions[i].X == 0 && right->Data().Directions[i].Y == 1 &&
                    right->Data().BaseForces[i] == ForceVector{0, 3600},
                "natural materials altered independent spatial payloads");
}

bool IsOre(CellTypeId material) {
    return material == CellTypeId::CoalOre || material == CellTypeId::CopperOre || material == CellTypeId::IronOre ||
           material == CellTypeId::TungstenOre || material == CellTypeId::GoldOre;
}

void NaturalStagesAndContent() {
    const auto settings = Natural();
    const WorldRect bounds{-129, 440, 256, 1024};
    const auto strata = SampleGeneration(settings, bounds, GenerationStage::Strata);
    const auto caves = SampleGeneration(settings, bounds, GenerationStage::Caves);
    const auto ores = SampleGeneration(settings, bounds, GenerationStage::Ores);
    const auto final = SampleGeneration(settings, bounds);
    Require(MaterialDigest(final) == 2088870111924836829ull, "natural/1 fixed material golden region changed");
    std::array<uint64_t, 51> counts{};
    uint64_t caveCount = 0;
    for (uint32_t x = 0; x < bounds.Width; ++x) {
        const auto column = SampleGenerationColumn(settings, bounds.X + x);
        for (uint32_t y = 0; y < bounds.Height; ++y) {
            const size_t index = size_t(y) * bounds.Width + x;
            const int64_t worldY = bounds.Y + y, depth = worldY - column.SurfaceY;
            ++counts[uint32_t(final[index])];
            Require(final[index] != CellTypeId::Dirt, "natural strata acquired dynamic legacy Dirt");
            Require(strata[index] == (depth < 0 ? CellTypeId::Air : depth < column.SoilThickness ? CellTypeId::GroundSoil : CellTypeId::Stone),
                    "strata differs from its complete surface and soil-thickness contract");
            if (caves[index] != strata[index]) {
                Require(strata[index] == CellTypeId::Stone && caves[index] == CellTypeId::Air && depth >= 48,
                        "cave replaced soil, broke its roof, or introduced material");
                ++caveCount;
            }
            if (ores[index] != caves[index])
                Require(caves[index] == CellTypeId::Stone && IsOre(ores[index]), "ore replaced a cave or non-Stone host");
            if (IsOre(ores[index])) {
                const int64_t minimum = ores[index] == CellTypeId::GoldOre ? 320 : ores[index] == CellTypeId::TungstenOre ? 384 :
                                        ores[index] == CellTypeId::IronOre ? 160 : ores[index] == CellTypeId::CopperOre ? 80 : 40;
                Require(depth >= minimum, "ore appeared above its versioned minimum depth");
            }
            if (caves[index] == CellTypeId::Air)
                Require(ores[index] == CellTypeId::Air, "ore filled an existing cave");
            if (final[index] == CellTypeId::Water)
                Require(ores[index] == CellTypeId::Air, "water overwrote a solid host");
            if (final[index] == CellTypeId::GrassSoil) {
                Require(worldY == column.SurfaceY && ores[index] == CellTypeId::GroundSoil && !column.LakeWaterLevel,
                        "grass appeared away from dry natural topsoil");
                Require(y > 0 && final[index - bounds.Width] == CellTypeId::Air, "grass has no dry exposed top face");
            }
        }
    }
    Require(caveCount > 0, "natural test region contains no actual caves");
    for (CellTypeId material : {CellTypeId::GroundSoil, CellTypeId::GrassSoil, CellTypeId::Stone, CellTypeId::CoalOre,
                                CellTypeId::CopperOre, CellTypeId::IronOre, CellTypeId::TungstenOre, CellTypeId::GoldOre, CellTypeId::Water})
        Require(counts[uint32_t(material)] > 0, "natural/1 failed to produce a required material in its content fixture");
    Require(MaterialDigest(SampleGeneration(Natural(3), bounds)) != MaterialDigest(final), "different world seeds generated identical content");

    const auto spawnHeight = CreateWorldGenerator(settings)->SurfaceHeight(512);
    const WorldRect spawn{496, spawnHeight - 8, 33, 12};
    const auto spawnCells = SampleGeneration(settings, spawn);
    for (uint32_t x = 0; x < spawn.Width; ++x) {
        Require(SampleGenerationColumn(settings, spawn.X + x).SurfaceY == spawnHeight, "spawn platform is not level across its full width");
        for (uint32_t y = 0; y < spawn.Height; ++y)
            Require(y < 8 ? spawnCells[size_t(y) * spawn.Width + x] == CellTypeId::Air
                          : spawnCells[size_t(y) * spawn.Width + x] == (y == 8 ? CellTypeId::GrassSoil : CellTypeId::GroundSoil),
                    "spawn platform lacks dry clearance or fixed support");
    }
}

void CheckWaterBody(WorldGenerationSettings settings, const GenerationWaterBody &body) {
    const WorldRect halo{body.Bounds.X - 1, body.Bounds.Y - 1, body.Bounds.Width + 2, body.Bounds.Height + 2};
    const auto cells = SampleGeneration(settings, halo);
    uint32_t volume = 0;
    bool freeSurface = false;
    for (uint32_t y = 1; y + 1 < halo.Height; ++y)
        for (uint32_t x = 1; x + 1 < halo.Width; ++x) {
            const size_t index = size_t(y) * halo.Width + x;
            if (cells[index] != CellTypeId::Water)
                continue;
            ++volume;
            Require(halo.Y + y >= body.Level, "water appeared above its fixed level");
            Require(cells[index - 1] != CellTypeId::Air && cells[index + 1] != CellTypeId::Air && cells[index + halo.Width] != CellTypeId::Air,
                    "initial water body has a sideways or downward four-neighbour leak");
            freeSurface |= halo.Y + y == body.Level && cells[index - halo.Width] == CellTypeId::Air;
            if (body.Kind == GenerationWaterKind::SurfaceLake && cells[index + halo.Width] != CellTypeId::Water)
                Require(cells[index + halo.Width] == CellTypeId::GroundSoil, "submerged lake floor acquired grass or lost soil");
        }
    Require(volume == body.CellCount && volume > 0 && freeSurface, "water plan volume or free surface differs from generated material");
    Require(volume <= (body.Kind == GenerationWaterKind::SurfaceLake ? 129 * 8 : 128 * 128), "water body exceeds finite capacity");
    CompareTiling(settings, halo);
}

void CheckActualChunkIntersection(WorldGenerationSettings settings, WorldRect bounds) {
    const auto reference = SampleGeneration(settings, bounds);
    const auto first = ChunkOf({bounds.X, bounds.Y});
    const auto last = ChunkOf({bounds.X + bounds.Width - 1, bounds.Y + bounds.Height - 1});
    const auto generator = CreateWorldGenerator(settings);
    for (int64_t cy = first.Y; cy <= last.Y; ++cy)
        for (int64_t cx = first.X; cx <= last.X; ++cx) {
            const auto chunk = generator->GenerateChunk({cx, cy});
            const int64_t x0 = std::max(bounds.X, cx * storage::ChunkEdge), y0 = std::max(bounds.Y, cy * storage::ChunkEdge);
            const int64_t x1 = std::min(bounds.X + bounds.Width, (cx + 1) * storage::ChunkEdge);
            const int64_t y1 = std::min(bounds.Y + bounds.Height, (cy + 1) * storage::ChunkEdge);
            for (int64_t y = y0; y < y1; ++y)
                for (int64_t x = x0; x < x1; ++x)
                    Require(chunk->Data().Materials[size_t(y - cy * storage::ChunkEdge) * storage::ChunkEdge + size_t(x - cx * storage::ChunkEdge)] ==
                                reference[size_t(y - bounds.Y) * bounds.Width + size_t(x - bounds.X)],
                            "water differs when its full body intersects real Chunk X/Y boundaries");
        }
}

void FiniteWaterAndFeatureBoundaries() {
    for (uint64_t seed : {1, 3, 42}) {
        const auto settings = Natural(seed);
        const auto bodies = PlanGenerationWater(settings, {-4096, 256, 8192, 3072});
        uint32_t lakes = 0, groundwater = 0;
        for (const auto &body : bodies) {
            if (body.Kind == GenerationWaterKind::SurfaceLake) {
                if (lakes++ == 0)
                    CheckWaterBody(settings, body);
            } else if (groundwater++ == 0) {
                Require(body.CellCount >= 32, "underground water accepted a subminimum component");
                CheckWaterBody(settings, body);
            }
        }
        Require(lakes > 0 && groundwater > 0, "multiple fixed seeds must each produce actual lakes and accepted groundwater");
    }

    // Seed 2 has a lake crossing the real x=2048 boundary of 1024 chunks.
    const auto crossingPlans = PlanGenerationWater(Natural(2), {2030, 480, 70, 96});
    const auto crossing = std::find_if(crossingPlans.begin(), crossingPlans.end(), [](const auto &body) {
        return body.Kind == GenerationWaterKind::SurfaceLake && body.Bounds.X < 2048 && body.Bounds.X + body.Bounds.Width > 2048;
    });
    Require(crossing != crossingPlans.end(), "water fixture no longer crosses a real 1024-cell horizontal boundary");
    CheckWaterBody(Natural(2), *crossing);
    auto translated = Natural(2);
    translated.SurfaceY += 505;
    const auto verticalPlans = PlanGenerationWater(translated, {2030, 1000, 70, 96});
    const auto vertical = std::find_if(verticalPlans.begin(), verticalPlans.end(), [](const auto &body) {
        return body.Kind == GenerationWaterKind::SurfaceLake && body.Bounds.Y < 1024 && body.Bounds.Y + body.Bounds.Height > 1024;
    });
    // A legal overall translation explicitly exercises a 1024-cell Y seam;
    // default surface water naturally only crosses 64-cell vertical seams.
    Require(vertical != verticalPlans.end(), "translated lake must cross a real 1024-cell vertical boundary");
    CheckWaterBody(translated, *vertical);
    CheckActualChunkIntersection(translated, {vertical->Bounds.X - 1, vertical->Bounds.Y - 1,
                                             vertical->Bounds.Width + 2, vertical->Bounds.Height + 2});

    auto dry = Natural(); dry.WaterEnabled = false;
    Require(PlanGenerationWater(dry, {-4096, 256, 8192, 3072}).empty(), "disabled water still produced a body plan");
    const auto dryCells = SampleGeneration(dry, {-129, 480, 256, 96});
    Require(std::find(dryCells.begin(), dryCells.end(), CellTypeId::Water) == dryCells.end(), "disabled water still filled cells");
    auto negativeSurface = Natural(); negativeSurface.SurfaceY = -512;
    for (const auto &body : PlanGenerationWater(negativeSurface, {-4096, -1024, 8192, 3072}))
        Require(body.Bounds.Height <= 128 && body.CellCount <= 16384, "negative surface broke bounded water geometry");
}
} // namespace

int main() {
    try {
        NoiseContractAndSettings();
        FlatCompatibilityAndPayload();
        CoordinatesTilingAndChunkPublication();
        NaturalStagesAndContent();
        FiniteWaterAndFeatureBoundaries();
        std::cout << "Deterministic generation, stages, finite water and chunk consistency tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
