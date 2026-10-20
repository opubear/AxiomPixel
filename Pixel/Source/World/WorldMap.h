#pragma once

#include "CellState.h"
#include "Core/Containers/Array.h"
#include "Core/Containers/Map.h"
#include "Core/Containers/StaticArray.h"
#include "Performance/MemoryUsage.h"
#include "WorldCoordinates.h"
#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace pixel {
class WorldMap;
class WorldSnapshot;

struct WorldMapView {
    uint32_t Width = 0, Height = 0;
    CellCoord Origin;
    const WorldMap *Map = nullptr;
    const WorldSnapshot *Snapshot = nullptr;

    size_t CellCount() const { return size_t(Width) * Height; }
    bool IsValid() const { return (Map != nullptr || Snapshot != nullptr) && Width && Height; }
    std::optional<CellState> TryAtLocal(uint32_t x, uint32_t y) const;
    CellState AtLocal(uint32_t x, uint32_t y) const;
    CellState AtIndex(size_t index) const;
    uint64_t RegionVersion(int64_t x, int64_t y) const;
    uint64_t Identity() const;
};

struct PackedDirection {
    int8_t X = 0, Y = 1;
};

struct MaterialMove {
    CellCoord Source, Destination;
    CellTypeId Type = CellTypeId::Dirt;
};

// One allocation, with material scans independent of direction and force storage.
struct ChunkPayload {
    axiom::StaticArray<CellTypeId, storage::ChunkCells> Materials;
    axiom::StaticArray<PackedDirection, storage::ChunkCells> Directions;
    axiom::StaticArray<ForceVector, storage::ChunkCells> BaseForces;
};
static_assert(sizeof(ChunkPayload) == storage::ChunkCells * 14);

class WorldChunk {
  public:
    explicit WorldChunk(CellCoord coordinate);
    WorldChunk(const WorldChunk &) = delete;
    WorldChunk &operator=(const WorldChunk &) = delete;
    CellCoord Coordinate;
    bool Dirty = false;
    uint64_t Revision = 1;
    axiom::StaticArray<uint64_t, storage::RegionsPerChunk> RegionVersions{};
    axiom::StaticArray<uint32_t, storage::RegionsPerChunk> MaterialCounts{};
    axiom::StaticArray<uint8_t, storage::RegionsPerChunk> Active{};

    ChunkPayload &Data() { return Payload.front(); }
    const ChunkPayload &Data() const { return Payload.front(); }
    performance::MemoryUsage GetMemoryUsage() const;
    void Initialize(std::optional<int64_t> surface);
    void Recount();

  private:
    axiom::Array<ChunkPayload, axiom::Allocator<ChunkPayload, storage::ChunkAlignment>> Payload;
};

class WorldMap {
  public:
    WorldMap(uint32_t width, uint32_t height);
    WorldMap();
    WorldMap(const WorldMap &) = delete;
    WorldMap &operator=(const WorldMap &) = delete;
    ~WorldMap();

    bool IsFinite() const { return Finite; }
    WorldRect Bounds() const { return Extent; }
    bool Contains(CellCoord cell) const;
    WorldMapView View(WorldRect bounds) const { return {bounds.Width, bounds.Height, {bounds.X, bounds.Y}, this}; }
    WorldMapView Read() const { return View(Extent); }
    std::optional<CellState> TryGet(CellCoord cell) const;
    CellState Get(CellCoord cell) const;
    ForceVector GetForce(CellCoord cell) const;
    void Set(CellCoord coordinate, CellState cell);
    void SetMaterial(CellCoord coordinate, CellTypeId type);
    void CommitMaterialMoves(std::span<const MaterialMove> moves);
    void SetForce(CellCoord coordinate, ForceVector force);
    void Fill(WorldRect region, CellState cell);
    void FillForce(WorldRect region, ForceVector force);
    void FillDirection(WorldRect region, ForceDirection direction);
    void Invalidate(WorldRect region);
    void Wake(WorldRect region);
    void WakeNeighbours(CellCoord cell);
    void Clear();
    void ResetInfinite() {
        Clear();
        Finite = false;
        Extent = {0, 0, storage::DefaultWindowEdge, storage::DefaultWindowEdge};
    }
    void GenerateTerrain(int64_t surface);
    uint64_t RegionVersion(CellCoord cell) const;
    void ForEachRegion(WorldRect bounds, const std::function<void(WorldRect, uint64_t)> &visitor) const;
    axiom::Array<WorldRect> TakeActiveRegions(WorldRect bounds, WorldRect dynamicForces = {});
    std::span<const CellTypeId> MaterialRow(CellCoord begin, uint32_t count) const;

    WorldChunk *FindChunk(CellCoord chunk);
    const WorldChunk *FindChunk(CellCoord chunk) const;
    void Insert(std::unique_ptr<WorldChunk> chunk);
    std::unique_ptr<WorldChunk> Remove(CellCoord chunk);
    axiom::Array<CellCoord> ChunkCoordinates() const;
    size_t ChunkCount() const { return Chunks.size(); }
    uint64_t GetIdentity() const { return Identity; }
    performance::MemoryUsage GetMemoryUsage() const;
    performance::MemoryUsage GetForceMemoryUsage() const;

  private:
    struct Node;
    std::unique_ptr<Node> Root;
    axiom::Map<CellCoord, std::unique_ptr<WorldChunk>, CellCoordLess> Chunks;
    WorldRect Extent;
    bool Finite = false;
    uint64_t Version = 1;
    uint64_t Identity = 0;
    static size_t Index(CellCoord cell);
    static size_t RegionIndex(CellCoord cell);
    void Changed(WorldChunk &chunk, CellCoord cell, bool material);
    void RebuildIndex();
    void RequireResident(WorldRect region) const;
};

class WorldSnapshot {
  public:
    explicit WorldSnapshot(uint32_t width = 0, uint32_t height = 0) : Extent{0, 0, width, height} {}
    void Update(const WorldMap &map, WorldRect bounds);
    WorldMapView Read() const { return {Extent.Width, Extent.Height, {Extent.X, Extent.Y}, nullptr, this}; }
    std::optional<CellState> TryGet(CellCoord cell) const;
    uint64_t RegionVersion(CellCoord cell) const;
    performance::MemoryUsage GetMemoryUsage() const;
    uint64_t GetMapIdentity() const { return MapIdentity; }

  private:
    struct Region {
        uint64_t Version = 0;
        axiom::Array<CellState> Cells;
    };
    WorldRect Extent;
    uint64_t MapIdentity = 0;
    std::optional<WorldRect> MapExtent;
    axiom::Map<CellCoord, Region, CellCoordLess> Regions;
};
} // namespace pixel
