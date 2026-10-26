#include "CellularMaterials.h"

namespace pixel::cellular {
namespace {
class Neighbours {
  public:
    Neighbours(const WorldMap &map, WorldRect bounds, CellCoord cell)
        : Map(map), Bounds(bounds), Cell(cell) {}

    CellCoord At(int x, int y) const { return {Cell.X + x, Cell.Y + y}; }

    bool Is(int x, int y, CellTypeId material) const {
        const auto position = At(x, y);
        const auto value = Bounds.Contains(position) ? Map.TryGet(position) : std::nullopt;
        return value && value->Type == material;
    }

    bool Empty(int x, int y) const { return Is(x, y, CellTypeId::Air); }

    bool Touches(CellTypeId material) const {
        return Is(0, -1, material) || Is(-1, 0, material)
            || Is(1, 0, material) || Is(0, 1, material);
    }

  private:
    const WorldMap &Map;
    WorldRect Bounds;
    CellCoord Cell;
};

uint64_t Noise(CellCoord cell, uint64_t step, CellTypeId material) {
    // Stateless noise makes retries and region traversal order irrelevant.
    uint64_t value = uint64_t(cell.X) * 0x9e3779b97f4a7c15ULL
        ^ uint64_t(cell.Y) * 0xd6e8feb86659fd93ULL
        ^ step * 0xa0761d6478bd642fULL ^ uint32_t(material);
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

std::optional<CellCoord> FlowWater(const Neighbours &near, int first) {
    if (near.Empty(0, 1))
        return near.At(0, 1);

    const bool stacked = near.Is(0, -1, CellTypeId::Water);
    const bool supportedByWater = near.Is(0, 1, CellTypeId::Water);
    for (const int dx : {first, -first}) {
        if (!near.Empty(dx, 0))
            continue;
        const bool drop = near.Empty(dx, 1);
        // Following a supporting water cell sideways translates entire
        // columns forever. Keep that column still until its support changes.
        if (drop && supportedByWater)
            continue;
        if (drop)
            return near.At(dx, 1);
        if (stacked)
            return near.At(dx, 0);
        // Look along a short open ledge, but still move only one cell. The
        // radius also defines the wake halo for changes to a distant outlet.
        for (int distance = 2; distance <= 4 && near.Empty(dx * distance, 0); ++distance)
            if (near.Empty(dx * distance, 1))
                return near.At(dx, 0);
    }
    return std::nullopt;
}

std::optional<CellCoord> Rise(const Neighbours &near, int first, bool drift) {
    // Both orthogonal passages must be open for a diagonal: wisps must not
    // leak through two touching wall corners.
    if (drift && near.Empty(first, 0) && near.Empty(0, -1) && near.Empty(first, -1))
        return near.At(first, -1);
    if (near.Empty(0, -1))
        return near.At(0, -1);
    for (const int dx : {first, -first})
        if (near.Empty(dx, 0))
            return near.At(dx, 0);
    return std::nullopt;
}
} // namespace

Update Evaluate(const WorldMap &map, WorldRect bounds, CellCoord cell,
                CellTypeId material, uint64_t step, WindVelocity wind) {
    const Neighbours near(map, bounds, cell);
    const auto noise = Noise(cell, step, material);
    const int first = (noise & 1) ? -1 : 1;
    if (material == CellTypeId::Water)
        return {material, FlowWater(near, first)};

    if (!IsTransient(material))
        throw std::logic_error("cellular rule requires water, steam or fire");

    // Decay is a reproducible visual rule, not temperature or fuel use.
    const uint64_t decayMask = material == CellTypeId::Fire ? 63 : 511;
    if ((noise & decayMask) == 0 || (material == CellTypeId::Fire && near.Touches(CellTypeId::Water)))
        return {CellTypeId::Air, std::nullopt};

    // Steam drifts more slowly; fire advances every successful fixed step.
    if (material == CellTypeId::Steam && ((noise >> 10) & 3) == 0)
        return {material, std::nullopt};
    const int direction = wind.X > 1 ? 1 : wind.X < -1 ? -1 : first;
    const bool drift = wind.X > 1 || wind.X < -1 || ((noise >> 12) & 3) == 0;
    return {material, Rise(near, direction, drift)};
}
} // namespace pixel::cellular
