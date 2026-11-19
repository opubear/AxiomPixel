#include "LightingCache.h"
#include "Runtime/Render/PixelWorldPresentation.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <thread>

namespace pixel::render {
namespace {
constexpr int Edge = LightTileEdge;
constexpr int Halo = LightRadius;
constexpr size_t TileCells = size_t(Edge) * Edge;
constexpr uint64_t WorkingBytes = 512 * 1024; // Bounded flood scratch and one bake in flight.
using Revisions = std::array<uint64_t, 9>;

uint32_t SampleDensity(WorldRect bounds) {
    return uint64_t(bounds.Width) * bounds.Height <= 512u * 512u ? 2 : 1;
}

OpticalCell Optics(CellTypeId type) {
    switch (type) {
    case CellTypeId::Air: case CellTypeId::Steam: case CellTypeId::UnlitTorch: return OpticalCell::Clear;
    case CellTypeId::LitTorch: return OpticalCell::Torch;
    case CellTypeId::Fire: return OpticalCell::Fire;
    case CellTypeId::Lava: return OpticalCell::Lava;
    case CellTypeId::Water: return OpticalCell::Water;
    case CellTypeId::Ice: return OpticalCell::Ice;
    case CellTypeId::Leaves: return OpticalCell::Leaves;
    default: return OpticalCell::Wall;
    }
}

bool DynamicEmitter(OpticalCell cell) { return cell == OpticalCell::Fire || cell == OpticalCell::Lava; }
OpticalCell ForBake(OpticalCell cell) { return DynamicEmitter(cell) ? OpticalCell::Clear : cell; }
OpticalCell ForFlood(OpticalCell cell) { return cell == OpticalCell::Torch ? OpticalCell::Clear : cell; }

struct Tile {
    std::array<OpticalCell, TileCells> Cells{};
    uint64_t MaterialVersion = 0, BakeInputVersion = 0, FloodInputVersion = 0;
    uint32_t Width = 0, Height = 0;
    bool HasPlaced = false, HasDynamic = false;

    Revisions BakeDependencies{}, FloodDependencies{};
    std::optional<CellCoord> Held;
    uint64_t BakeVersion = 0, OutputVersion = 0;
    uint32_t Samples = 0, StableFrames = 0;
    std::vector<LightTexel> Preview, Baked, Dynamic;
};
using Tiles = std::map<CellCoord, Tile, CellCoordLess>;

void ReadOptics(Tile &tile, WorldMapView world, uint32_t startX, uint32_t startY, uint64_t &revision) {
    const auto width = std::min(LightTileEdge, world.Width - startX);
    const auto height = std::min(LightTileEdge, world.Height - startY);
    const auto version = GridSourceRegionVersion(world, startX, startY, width, height);
    if (tile.MaterialVersion == version && tile.Width == width && tile.Height == height && tile.BakeInputVersion != 0) return;

    bool bakeChanged = tile.BakeInputVersion == 0, floodChanged = tile.FloodInputVersion == 0;
    tile.HasPlaced = tile.HasDynamic = false;
    const auto store = [&](uint32_t x, uint32_t y, OpticalCell cell) {
        auto &previous = tile.Cells[size_t(y) * Edge + x];
        bakeChanged |= ForBake(previous) != ForBake(cell);
        floodChanged |= ForFlood(previous) != ForFlood(cell);
        tile.HasPlaced |= cell == OpticalCell::Torch;
        tile.HasDynamic |= DynamicEmitter(cell);
        previous = cell;
    };
    for (uint32_t y = 0; y < LightTileEdge; ++y) {
        const CellCoord begin{world.Origin.X + startX, world.Origin.Y + startY + y};
        const CellCoord end{begin.X + width - 1, begin.Y};
        if (y < height && world.Map && ChunkOf(begin) == ChunkOf(end)
            && world.Map->TryGetMaterial(begin) && world.Map->TryGetMaterial(end)) {
            const auto row = world.Map->MaterialRow(begin, width);
            for (uint32_t x = 0; x < width; ++x) store(x, y, Optics(row[x]));
        } else {
            for (uint32_t x = 0; y < height && x < width; ++x) {
                const auto cell = world.TryAtLocal(startX + x, startY + y);
                store(x, y, cell ? Optics(cell->Type) : OpticalCell::Missing);
            }
        }
        for (uint32_t x = y < height ? width : 0; x < LightTileEdge; ++x) store(x, y, OpticalCell::Missing);
    }
    tile.MaterialVersion = version;
    tile.Width = width;
    tile.Height = height;
    if (bakeChanged) tile.BakeInputVersion = ++revision;
    if (floodChanged) tile.FloodInputVersion = ++revision;
}

struct Neighbourhood {
    std::array<const Tile *, 9> Tiles{};
    Revisions StaticVersions{}, DynamicVersions{};
    bool HasPlaced = false, HasDynamic = false;
};

Neighbourhood FindNeighbours(const Tiles &tiles, CellCoord center) {
    Neighbourhood result;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto it = tiles.find({center.X + dx * Edge, center.Y + dy * Edge});
            if (it == tiles.end()) continue;
            const auto &tile = it->second;
            const size_t index = size_t(dy + 1) * 3 + dx + 1;
            result.Tiles[index] = &tile;
            result.StaticVersions[index] = tile.BakeInputVersion;
            result.DynamicVersions[index] = tile.FloodInputVersion;
            result.HasPlaced |= tile.HasPlaced;
            result.HasDynamic |= tile.HasDynamic;
        }
    }
    return result;
}

LightPatch CopyPatch(const Neighbourhood &near, CellCoord origin, std::optional<CellCoord> held = {}) {
    LightPatch patch;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const auto *tile = near.Tiles[size_t(dy + 1) * 3 + dx + 1];
            if (!tile) continue;
            const int patchX = Halo + dx * Edge, patchY = Halo + dy * Edge;
            const int left = std::max(0, patchX), right = std::min(int(LightPatchEdge), patchX + Edge);
            const int top = std::max(0, patchY), bottom = std::min(int(LightPatchEdge), patchY + Edge);
            for (int y = top; y < bottom; ++y)
                std::copy_n(tile->Cells.data() + (y - patchY) * Edge + left - patchX, right - left,
                            patch.Cells.data() + y * LightPatchEdge + left);
        }
    }
    if (held) patch.HeldTorch = size_t(held->Y - origin.Y + Halo) * LightPatchEdge + size_t(held->X - origin.X + Halo);
    return patch;
}

struct BakeResult {
    CellCoord Origin;
    uint64_t Version = 0;
    std::vector<LightTexel> Texels;
};

class Baker {
  public:
    bool Busy() const { return Result.valid(); }
    std::optional<BakeResult> Poll() {
        if (Busy() && Result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) return Result.get();
        return {};
    }
    void Submit(CellCoord origin, uint64_t version, LightPatch patch, uint32_t samples) {
        if (Busy()) throw std::logic_error("light baker already has work");
        if (!Worker.joinable()) Worker = std::jthread([this](std::stop_token stop) { Run(stop); });
        std::packaged_task<BakeResult(std::stop_token)> task(
            [origin, version, patch = std::move(patch), samples](std::stop_token stop) {
                return BakeResult{origin, version, BakeLightTile(patch, samples, stop)};
            });
        Result = task.get_future();
        {
            std::lock_guard lock(Mutex);
            Pending = std::move(task);
        }
        Wake.notify_one();
    }
  private:
    void Run(std::stop_token stop) {
        std::unique_lock lock(Mutex);
        while (Wake.wait(lock, stop, [this] { return Pending.valid(); }) && !stop.stop_requested()) {
            auto task = std::move(Pending);
            lock.unlock();
            task(stop);
            lock.lock();
        }
    }
    std::mutex Mutex;
    std::condition_variable_any Wake;
    std::packaged_task<BakeResult(std::stop_token)> Pending;
    std::future<BakeResult> Result;
    std::jthread Worker;
};

void UpdateTile(Tile &tile, const Neighbourhood &near, CellCoord origin, std::optional<CellCoord> held,
                uint32_t samples, uint64_t &revision) {
    const WorldRect footprint{origin.X - Halo, origin.Y - Halo, LightPatchEdge, LightPatchEdge};
    if (held && !footprint.Contains(*held)) held.reset();
    const bool staticChanged = tile.Samples != samples || tile.BakeDependencies != near.StaticVersions;
    const bool dynamicChanged = tile.FloodDependencies != near.DynamicVersions || tile.Held != held;
    if (!staticChanged && !dynamicChanged) {
        tile.StableFrames = std::min(2u, tile.StableFrames + 1);
        return;
    }
    std::optional<LightPatch> patch;
    if ((staticChanged && near.HasPlaced) || (dynamicChanged && (near.HasDynamic || held)))
        patch = CopyPatch(near, origin, held);
    if (staticChanged) {
        tile.BakeDependencies = near.StaticVersions;
        tile.BakeVersion = ++revision;
        tile.StableFrames = 0;
        tile.Samples = samples;
        tile.Baked = {};
        tile.Preview = near.HasPlaced && std::ranges::find(patch->Cells, OpticalCell::Torch) != patch->Cells.end()
            ? FloodLightTile(*patch, LightSources::Placed) : std::vector<LightTexel>{};
    } else {
        tile.StableFrames = std::min(2u, tile.StableFrames + 1);
    }
    if (dynamicChanged) {
        tile.FloodDependencies = near.DynamicVersions;
        tile.Held = held;
        const bool hasEmitter = held || (near.HasDynamic && std::ranges::any_of(patch->Cells, DynamicEmitter));
        tile.Dynamic = hasEmitter ? FloodLightTile(*patch, LightSources::Dynamic) : std::vector<LightTexel>{};
    }
    tile.OutputVersion = ++revision;
}

void WriteTile(Lightmap &output, const Tile &tile, uint32_t x, uint32_t y, WorldRect bounds, bool visible) {
    const uint32_t samples = output.SamplesPerCell;
    const uint32_t width = std::min(LightTileEdge, bounds.Width - x) * samples;
    const uint32_t height = std::min(LightTileEdge, bounds.Height - y) * samples;
    const uint32_t stride = bounds.Width * samples;
    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t cell = size_t(row / samples) * Edge + col / samples;
            LightTexel light{};
            if (visible) {
                if (!tile.Baked.empty()) light = tile.Baked[size_t(row) * Edge * samples + col];
                else if (!tile.Preview.empty()) light = tile.Preview[cell];
                if (!tile.Dynamic.empty())
                    for (size_t channel = 0; channel < 3; ++channel)
                        light[channel] = uint8_t(std::min(255, int(light[channel]) + tile.Dynamic[cell][channel]));
            }
            output.Texels[size_t(y * samples + row) * stride + x * samples + col] = light;
        }
    }
}
} // namespace

struct LightingCache::State {
    Tiles Regions;
    uint64_t MapIdentity = 0, Revision = 0;
    WorldRect Projection;
    Baker Worker;
};

LightingCache::LightingCache() : Cache(std::make_unique<State>()) {}
LightingCache::~LightingCache() = default;

void LightingCache::Build(WorldMapView world, WorldRect visible, std::optional<CellCoord> heldTorch, Lightmap &output) {
    auto &state = *Cache;
    const WorldRect bounds{world.Origin.X, world.Origin.Y, world.Width, world.Height};
    const uint32_t samples = SampleDensity(bounds);
    if (state.MapIdentity != world.Identity()) {
        state.Regions.clear();
        state.MapIdentity = world.Identity();
    }
    if (bounds != state.Projection) {
        std::erase_if(state.Regions, [&](const auto &entry) {
            const auto point = entry.first;
            return !bounds.Contains(point) || (point.X - bounds.X) % Edge || (point.Y - bounds.Y) % Edge;
        });
        output.RegionVersions.clear();
        state.Projection = bounds;
    }
    for (uint32_t y = 0; y < bounds.Height; y += Edge)
        for (uint32_t x = 0; x < bounds.Width; x += Edge)
            ReadOptics(state.Regions[{bounds.X + x, bounds.Y + y}], world, x, y, state.Revision);

    auto completed = state.Worker.Poll();
    std::optional<CellCoord> next;
    int64_t bestDistance = INT64_MAX;
    bool any = false;
    for (auto &[origin, tile] : state.Regions) {
        if (!visible.Intersects({origin.X, origin.Y, LightTileEdge, LightTileEdge})) continue;
        const auto near = FindNeighbours(state.Regions, origin);
        UpdateTile(tile, near, origin, heldTorch, samples, state.Revision);
        if (completed && completed->Origin == origin && completed->Version == tile.BakeVersion) {
            tile.Baked = std::move(completed->Texels);
            tile.OutputVersion = ++state.Revision;
        }
        any |= !tile.Preview.empty() || !tile.Dynamic.empty() || !tile.Baked.empty();
        if (tile.Preview.empty() || !tile.Baked.empty() || tile.StableFrames < 2 || state.Worker.Busy()) continue;
        const int64_t distance = std::abs(origin.X + Edge / 2 - (visible.X + visible.Width / 2))
                               + std::abs(origin.Y + Edge / 2 - (visible.Y + visible.Height / 2));
        if (distance < bestDistance) {
            next = origin;
            bestDistance = distance;
        }
    }
    if (next) {
        const auto near = FindNeighbours(state.Regions, *next);
        state.Worker.Submit(*next, state.Regions.at(*next).BakeVersion, CopyPatch(near, *next), samples);
    }
    output.Enabled = any;
    if (!any) return;

    const size_t columns = (bounds.Width + Edge - 1) / Edge, rows = (bounds.Height + Edge - 1) / Edge;
    const size_t texelCount = size_t(bounds.Width) * bounds.Height * samples * samples;
    if (output.SamplesPerCell != samples || output.RegionVersions.size() != rows * columns || output.Texels.size() != texelCount) {
        output.SamplesPerCell = samples;
        output.RegionVersions.assign(rows * columns, 0);
        output.Texels.assign(texelCount, {});
    }
    for (const auto &[origin, tile] : state.Regions) {
        const auto x = uint32_t(origin.X - bounds.X), y = uint32_t(origin.Y - bounds.Y);
        const auto index = (y / Edge) * columns + x / Edge;
        const bool drawn = visible.Intersects({origin.X, origin.Y, LightTileEdge, LightTileEdge});
        const uint64_t version = drawn ? tile.OutputVersion : 0;
        if (output.RegionVersions[index] == version) continue;
        WriteTile(output, tile, x, y, bounds, drawn);
        output.RegionVersions[index] = version;
    }
}

uint64_t LightingCache::CapacityFor(WorldRect bounds) {
    const uint64_t tiles = uint64_t((bounds.Width + Edge - 1) / Edge) * ((bounds.Height + Edge - 1) / Edge);
    const uint64_t samples = SampleDensity(bounds);
    const uint64_t perTile = sizeof(Tile) + 4 * sizeof(void *) + TileCells * sizeof(LightTexel) * (2 + 2 * samples * samples);
    return WorkingBytes + tiles * perTile;
}

uint64_t LightingCache::ReservedBytes() const {
    uint64_t bytes = WorkingBytes + Cache->Regions.size() * (sizeof(Tile) + 4 * sizeof(void *));
    for (const auto &[key, tile] : Cache->Regions)
        bytes += (tile.Preview.capacity() + tile.Baked.capacity() + tile.Dynamic.capacity()) * sizeof(LightTexel);
    return bytes;
}
} // namespace pixel::render
