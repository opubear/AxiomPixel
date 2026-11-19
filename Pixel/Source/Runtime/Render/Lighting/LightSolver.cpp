#include "LightSolver.h"
#include <algorithm>
#include <cmath>

namespace pixel::render {
namespace {
constexpr LightTexel Torch{LightUnit, 46, 29, 0};
constexpr LightTexel Fire{44, 25, 9, 0};
constexpr LightTexel Lava{56, 30, 10, 0};
constexpr int Edge = LightPatchEdge;
constexpr int Halo = LightRadius;
constexpr size_t Index(int x, int y) { return size_t(y) * Edge + x; }

constexpr bool Opaque(OpticalCell cell) { return cell == OpticalCell::Missing || cell == OpticalCell::Wall; }
constexpr int Absorption(OpticalCell cell) {
    switch (cell) {
    case OpticalCell::Water: return 4;
    case OpticalCell::Ice: return 2;
    case OpticalCell::Leaves: return 3;
    default: return 0;
    }
}

LightTexel Seed(const LightPatch &patch, size_t index, LightSources sources) {
    const auto material = patch.Cells[index];
    if (sources == LightSources::Placed) return material == OpticalCell::Torch ? Torch : LightTexel{};
    if (Opaque(material)) return {};
    if (patch.HeldTorch == index) return Torch;
    if (material == OpticalCell::Fire) return Fire;
    if (material == OpticalCell::Lava) return Lava;
    return {};
}

// Traverse full grid squares from the flame (cell centre) to the sample.
// Distances use quarter cells, so corner crossings can be compared exactly.
float Transmission(const LightPatch &patch, int x, int y, int sampleX, int sampleY) {
    const int targetX = sampleX / 4, targetY = sampleY / 4;
    const int dx = sampleX - (4 * x + 2), dy = sampleY - (4 * y + 2);
    const int distanceX = std::abs(dx), distanceY = std::abs(dy);
    const int stepX = dx < 0 ? -1 : 1, stepY = dy < 0 ? -1 : 1;
    int nextX = 2, nextY = 2;
    float transmission = 1.f;
    while (x != targetX || y != targetY) {
        const int crossX = x == targetX ? INT32_MAX : nextX * distanceY;
        const int crossY = y == targetY ? INT32_MAX : nextY * distanceX;
        if (crossX == crossY && (Opaque(patch.Cells[Index(x + stepX, y)]) || Opaque(patch.Cells[Index(x, y + stepY)])))
            return 0;
        if (crossX <= crossY) { x += stepX; nextX += 4; }
        if (crossY <= crossX) { y += stepY; nextY += 4; }
        // The first wall receives light on its exposed surface.
        if (x == targetX && y == targetY) break;
        const auto material = patch.Cells[Index(x, y)];
        if (Opaque(material)) return 0;
        transmission *= 1.f - float(Absorption(material)) / 16.f;
    }
    return transmission;
}
} // namespace

std::vector<LightTexel> FloodLightTile(const LightPatch &patch, LightSources sources) {
    constexpr int StepLoss = LightUnit / LightRadius;
    std::vector<LightTexel> field(patch.Cells.size());
    std::array<std::vector<uint32_t>, LightUnit + 1> buckets;
    // A channel only visits a cell at its highest reachable level. All sources
    // share the same queue and cost model for that channel.
    for (size_t channel = 0; channel < 3; ++channel) {
        for (uint32_t i = 0; i < patch.Cells.size(); ++i) {
            const auto level = Seed(patch, i, sources)[channel];
            if (!level) continue;
            field[i][channel] = level;
            buckets[level].push_back(i);
        }
        for (int level = LightUnit; level > StepLoss; --level) {
            for (const auto index : buckets[level]) {
                if (field[index][channel] != level) continue;
                const auto spread = [&](uint32_t to) {
                    const auto material = patch.Cells[to];
                    if (material == OpticalCell::Missing) return;
                    const int candidate = level - StepLoss - Absorption(material);
                    if (candidate <= field[to][channel]) return;
                    field[to][channel] = uint8_t(candidate);
                    if (!Opaque(material)) buckets[candidate].push_back(to);
                };
                if (index % Edge != 0) spread(index - 1);
                if (index % Edge != Edge - 1) spread(index + 1);
                if (index >= Edge) spread(index - Edge);
                if (index + Edge < patch.Cells.size()) spread(index + Edge);
            }
            buckets[level].clear();
        }
        for (int level = 1; level <= StepLoss; ++level) buckets[level].clear();
    }

    std::vector<LightTexel> tile(size_t(LightTileEdge) * LightTileEdge);
    for (uint32_t y = 0; y < LightTileEdge; ++y)
        std::copy_n(field.begin() + Index(Halo, Halo + y), LightTileEdge, tile.begin() + size_t(y) * LightTileEdge);
    return tile;
}

std::vector<LightTexel> BakeLightTile(const LightPatch &patch, uint32_t samplesPerCell, std::stop_token stop) {
    if (samplesPerCell != 1 && samplesPerCell != 2)
        throw std::invalid_argument("light bake requires one or two samples per axis");
    const int samples = int(samplesPerCell), side = int(LightTileEdge) * samples;
    constexpr int RadiusSquared = (4 * Halo) * (4 * Halo);
    const int stride = 4 / samples;
    std::vector<LightTexel> tile(size_t(side) * side);
    for (int y = 0; y < Edge; ++y) {
        if (stop.stop_requested()) return {};
        for (int x = 0; x < Edge; ++x) {
            if (patch.Cells[Index(x, y)] != OpticalCell::Torch) continue;
            const int left = std::max(0, (x - 2 * Halo) * samples);
            const int top = std::max(0, (y - 2 * Halo) * samples);
            const int right = std::min(side, (x + 1) * samples);
            const int bottom = std::min(side, (y + 1) * samples);
            for (int ty = top; ty < bottom; ++ty) {
                const int sampleY = Halo * 4 + stride * ty + stride / 2;
                const int dy = sampleY - (4 * y + 2);
                for (int tx = left; tx < right; ++tx) {
                    const int sampleX = Halo * 4 + stride * tx + stride / 2;
                    const int dx = sampleX - (4 * x + 2);
                    const int distance = dx * dx + dy * dy;
                    if (distance >= RadiusSquared || patch.Cells[Index(sampleX / 4, sampleY / 4)] == OpticalCell::Missing)
                        continue;
                    auto &destination = tile[size_t(ty) * side + tx];
                    if (destination[0] == 255 && destination[1] == 255 && destination[2] == 255) continue;
                    const float falloff = 1.f - float(distance) / RadiusSquared;
                    const float light = falloff * falloff * Transmission(patch, x, y, sampleX, sampleY);
                    for (size_t channel = 0; channel < 3; ++channel)
                        destination[channel] = uint8_t(std::min(255, int(destination[channel]) + int(std::lround(Torch[channel] * light))));
                }
            }
        }
    }
    return tile;
}
} // namespace pixel::render
