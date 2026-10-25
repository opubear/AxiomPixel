#include "WorldMap.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace pixel {
namespace {
constexpr int64_t CoordinateLimit = int64_t{1} << 60;
std::atomic<uint64_t> NextMapIdentity{1};

void ValidateCoordinate(CellCoord cell) {
    if (cell.X <= -CoordinateLimit || cell.X >= CoordinateLimit || cell.Y <= -CoordinateLimit || cell.Y >= CoordinateLimit)
        throw std::out_of_range("world coordinate exceeds supported range");
}

void ValidateRect(WorldRect rect) {
    ValidateCoordinate({rect.X, rect.Y});
    ValidateCoordinate({rect.X + rect.Width, rect.Y + rect.Height});
}

void ValidateAlignment() {
    static const bool valid = [] {
#if defined(_WIN32)
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        const size_t page = info.dwPageSize;
#else
        const long page = sysconf(_SC_PAGESIZE);
#endif
        if (page <= 0 || storage::ChunkAlignment % size_t(page) != 0)
            throw std::runtime_error("chunk alignment must be a multiple of the system page size");
        return true;
    }();
    (void)valid;
}

size_t LocalIndex(CellCoord cell) {
    const auto chunk = ChunkOf(cell);
    return size_t(cell.Y - chunk.Y * storage::ChunkEdge) * storage::ChunkEdge + size_t(cell.X - chunk.X * storage::ChunkEdge);
}

size_t LocalRegion(CellCoord cell) {
    const auto index = LocalIndex(cell);
    return (index / storage::ChunkEdge / storage::RegionEdge) * storage::RegionsPerAxis + (index % storage::ChunkEdge / storage::RegionEdge);
}

WorldRect Intersection(WorldRect a, WorldRect b) {
    const auto x = std::max(a.X, b.X), y = std::max(a.Y, b.Y);
    const auto endX = std::min(a.X + a.Width, b.X + b.Width);
    const auto endY = std::min(a.Y + a.Height, b.Y + b.Height);
    return {x, y, uint32_t(std::max<int64_t>(0, endX - x)), uint32_t(std::max<int64_t>(0, endY - y))};
}
} // namespace

WorldChunk::WorldChunk(CellCoord coordinate) : Coordinate(coordinate) {
    if (coordinate.X <= -CoordinateLimit / storage::ChunkEdge || coordinate.X >= CoordinateLimit / storage::ChunkEdge || coordinate.Y <= -CoordinateLimit / storage::ChunkEdge || coordinate.Y >= CoordinateLimit / storage::ChunkEdge)
        throw std::out_of_range("chunk coordinate exceeds supported range");
    ValidateAlignment();
    Payload.resize(1);
    Initialize(std::nullopt);
}

void WorldChunk::Initialize(std::optional<int64_t> surface) {
    auto &data = Data();
    std::fill(data.Materials.begin(), data.Materials.end(), CellTypes::Empty);
    std::fill(data.Directions.begin(), data.Directions.end(), PackedDirection{});
    std::fill(data.BaseForces.begin(), data.BaseForces.end(), ForceVector{0, 3600});
    if (surface) {
        const int64_t local = std::clamp(*surface - Coordinate.Y * storage::ChunkEdge, int64_t{0}, int64_t(storage::ChunkEdge));
        std::fill(data.Materials.begin() + local * storage::ChunkEdge, data.Materials.end(), CellTypes::Dirt);
    }
    RegionVersions.fill(1);
    Active.fill(1);
    Recount();
}

void WorldChunk::Recount() {
    MaterialCounts.fill(0);
    for (uint32_t y = 0; y < storage::ChunkEdge; ++y)
        for (uint32_t x = 0; x < storage::ChunkEdge; ++x)
            if (Data().Materials[size_t(y) * storage::ChunkEdge + x] != CellTypes::Empty)
                ++MaterialCounts[size_t(y / storage::RegionEdge) * storage::RegionsPerAxis + x / storage::RegionEdge];
}

performance::MemoryUsage WorldChunk::GetMemoryUsage() const {
    return {sizeof(*this), sizeof(ChunkPayload), storage::ChunkReservedBytes};
}

struct WorldMap::Node {
    int64_t X, Y, Edge;
    uint64_t Version = 0, Materials = 0;
    bool Active = false;
    WorldChunk *Chunk = nullptr;
    axiom::StaticArray<std::unique_ptr<Node>, 4> Children{};

    bool Contains(CellCoord cell) const {
        return cell.X >= X && cell.Y >= Y && cell.X - X < Edge && cell.Y - Y < Edge;
    }
    bool Intersects(WorldRect rect) const {
        return X < rect.X + rect.Width && rect.X < X + Edge && Y < rect.Y + rect.Height && rect.Y < Y + Edge;
    }
    size_t Quadrant(CellCoord cell) const {
        return size_t(cell.X >= X + Edge / 2) + 2 * size_t(cell.Y >= Y + Edge / 2);
    }
    void Aggregate() {
        if (Edge == storage::RegionEdge) {
            const auto index = LocalRegion({X, Y});
            Version = Chunk->RegionVersions[index];
            Materials = Chunk->MaterialCounts[index];
            Active = Chunk->Active[index] != 0;
            return;
        }
        Version = Materials = 0;
        Active = false;
        for (const auto &child : Children) {
            if (!child)
                continue;
            Version = std::max(Version, child->Version);
            Materials += child->Materials;
            Active |= child->Active;
        }
    }
    void Add(CellCoord cell, WorldChunk *chunk) {
        if (Edge == storage::RegionEdge) {
            Chunk = chunk;
        } else {
            const auto q = Quadrant(cell);
            if (!Children[q])
                Children[q] = std::make_unique<Node>(Node{X + int64_t(q % 2) * (Edge / 2), Y + int64_t(q / 2) * (Edge / 2), Edge / 2});
            Children[q]->Add(cell, chunk);
        }
        Aggregate();
    }
    void Refresh(CellCoord cell) {
        if (Edge > storage::RegionEdge) {
            auto &child = Children[Quadrant(cell)];
            if (child)
                child->Refresh(cell);
        }
        Aggregate();
    }
    void RefreshAll() {
        for (auto &child : Children)
            if (child)
                child->RefreshAll();
        Aggregate();
    }
    void Visit(WorldRect bounds, const std::function<void(Node &)> &visitor, bool activeOnly = false, WorldRect dynamicForces = {}) {
        const bool dynamic = !dynamicForces.Empty() && Intersects(dynamicForces);
        if (!Intersects(bounds) || (activeOnly && !Active && !dynamic))
            return;
        if (Edge == storage::RegionEdge) {
            visitor(*this);
        } else {
            for (auto &child : Children)
                if (child)
                    child->Visit(bounds, visitor, activeOnly, dynamicForces);
        }
        Aggregate();
    }
    size_t Count() const {
        size_t count = 1;
        for (const auto &child : Children)
            if (child)
                count += child->Count();
        return count;
    }
};

WorldMap::WorldMap(uint32_t width, uint32_t height)
    : Extent{0, 0, width, height}, Finite(true), Identity(NextMapIdentity.fetch_add(1)) {
    if (!width || !height || uint64_t(width) * height > (uint64_t{1} << 32))
        throw std::invalid_argument("finite world dimensions must be nonempty and supported");
    for (uint64_t y = 0; y < height; y += storage::ChunkEdge)
        for (uint64_t x = 0; x < width; x += storage::ChunkEdge)
            Insert(std::make_unique<WorldChunk>(ChunkOf({int64_t(x), int64_t(y)})));
}

WorldMap::WorldMap() : Extent{0, 0, storage::DefaultWindowEdge, storage::DefaultWindowEdge},
                       Identity(NextMapIdentity.fetch_add(1)) {}
WorldMap::~WorldMap() = default;

bool WorldMap::Contains(CellCoord cell) const {
    return cell.X > -CoordinateLimit && cell.X < CoordinateLimit && cell.Y > -CoordinateLimit && cell.Y < CoordinateLimit && (!Finite || Extent.Contains(cell));
}

size_t WorldMap::Index(CellCoord cell) { return LocalIndex(cell); }
size_t WorldMap::RegionIndex(CellCoord cell) { return LocalRegion(cell); }

WorldChunk *WorldMap::FindChunk(CellCoord chunk) {
    const auto found = Chunks.find(chunk);
    return found == Chunks.end() ? nullptr : found->second.get();
}

const WorldChunk *WorldMap::FindChunk(CellCoord chunk) const {
    const auto found = Chunks.find(chunk);
    return found == Chunks.end() ? nullptr : found->second.get();
}

std::optional<CellState> WorldMap::TryGet(CellCoord cell) const {
    if (!Contains(cell))
        return std::nullopt;
    const auto *chunk = FindChunk(ChunkOf(cell));
    if (!chunk)
        return std::nullopt;
    const auto index = Index(cell);
    const auto direction = chunk->Data().Directions[index];
    return CellState{chunk->Data().Materials[index], {direction.X, direction.Y}};
}

CellState WorldMap::Get(CellCoord cell) const {
    const auto result = TryGet(cell);
    if (!result)
        throw std::out_of_range("world cell is outside bounds or not resident");
    return *result;
}

ForceVector WorldMap::GetForce(CellCoord cell) const {
    if (!Contains(cell))
        throw std::out_of_range("force coordinate is outside the world");
    const auto *chunk = FindChunk(ChunkOf(cell));
    if (!chunk)
        throw std::out_of_range("force cell is not resident");
    return chunk->Data().BaseForces[Index(cell)];
}

void WorldMap::Changed(WorldChunk &chunk, CellCoord cell, bool material) {
    chunk.Dirty = true;
    chunk.Revision = ++Version;
    chunk.RegionVersions[RegionIndex(cell)] = Version;
    if (material && Root)
        Root->Refresh(cell);
    WakeNeighbours(cell);
}

void WorldMap::Set(CellCoord coordinate, CellState cell) {
    if (!cell.Force.IsValid())
        throw std::invalid_argument("force direction must select a neighbour or zero");
    Get(coordinate);
    auto &chunk = *FindChunk(ChunkOf(coordinate));
    const auto index = Index(coordinate), region = RegionIndex(coordinate);
    const auto before = chunk.Data().Materials[index];
    chunk.MaterialCounts[region] -= before != CellTypes::Empty;
    chunk.MaterialCounts[region] += cell.Type != CellTypes::Empty;
    chunk.Data().Materials[index] = cell.Type;
    chunk.Data().Directions[index] = {int8_t(cell.Force.X), int8_t(cell.Force.Y)};
    chunk.Data().BaseForces[index] = {3600.f * cell.Force.X, 3600.f * cell.Force.Y};
    Changed(chunk, coordinate, true);
}

void WorldMap::SetMaterial(CellCoord coordinate, CellTypeId type) {
    Get(coordinate);
    auto &chunk = *FindChunk(ChunkOf(coordinate));
    auto &old = chunk.Data().Materials[Index(coordinate)];
    if (old == type)
        return;
    auto &count = chunk.MaterialCounts[RegionIndex(coordinate)];
    count -= old != CellTypes::Empty;
    count += type != CellTypes::Empty;
    old = type;
    Changed(chunk, coordinate, true);
}

void WorldMap::SetForce(CellCoord coordinate, ForceVector force) {
    if (!std::isfinite(force.X) || !std::isfinite(force.Y))
        throw std::invalid_argument("force must be finite");
    Get(coordinate);
    auto &chunk = *FindChunk(ChunkOf(coordinate));
    if (chunk.Data().BaseForces[Index(coordinate)] == force)
        return;
    chunk.Data().BaseForces[Index(coordinate)] = force;
    Changed(chunk, coordinate, false);
}

void WorldMap::CommitMaterialMoves(std::span<const MaterialMove> moves) {
    if (moves.empty())
        return;
    // Validate against the same step-start state before any source is cleared.
    for (const auto &move : moves) {
        const auto source = TryGet(move.Source), target = TryGet(move.Destination);
        if (!source || !target || source->Type != move.Type || target->Type != CellTypeId::Air)
            throw std::logic_error("material commit requires matching sources and empty resident targets");
    }
    const auto revision = ++Version;
    WorldChunk *sourceChunk = nullptr, *destinationChunk = nullptr;
    CellCoord sourceKey, destinationKey;
    for (const auto &move : moves) {
        const auto source = ChunkOf(move.Source), destination = ChunkOf(move.Destination);
        if (!sourceChunk || source != sourceKey) {
            sourceKey = source;
            sourceChunk = FindChunk(source);
        }
        if (!destinationChunk || destination != destinationKey) {
            destinationKey = destination;
            destinationChunk = FindChunk(destination);
        }
        if (!sourceChunk || !destinationChunk)
            throw std::logic_error("material commit requires pinned resident chunks");
        sourceChunk->Data().Materials[Index(move.Source)] = CellTypes::Empty;
        destinationChunk->Data().Materials[Index(move.Destination)] = move.Type;
        --sourceChunk->MaterialCounts[RegionIndex(move.Source)];
        ++destinationChunk->MaterialCounts[RegionIndex(move.Destination)];
        sourceChunk->RegionVersions[RegionIndex(move.Source)] = revision;
        destinationChunk->RegionVersions[RegionIndex(move.Destination)] = revision;
        sourceChunk->Revision = destinationChunk->Revision = revision;
        sourceChunk->Dirty = destinationChunk->Dirty = true;
    }
    // Wake at region granularity once, rather than updating the tree per particle.
    for (auto &[coordinate, chunk] : Chunks) {
        for (uint32_t y = 0; y < storage::RegionsPerAxis; ++y) {
            for (uint32_t x = 0; x < storage::RegionsPerAxis; ++x) {
                if (chunk->RegionVersions[size_t(y) * storage::RegionsPerAxis + x] != revision)
                    continue;
                const CellCoord centre{coordinate.X * storage::ChunkEdge + x * storage::RegionEdge,
                                       coordinate.Y * storage::ChunkEdge + y * storage::RegionEdge};
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const CellCoord neighbour{centre.X + dx * int64_t(storage::RegionEdge), centre.Y + dy * int64_t(storage::RegionEdge)};
                        if (auto *owner = FindChunk(ChunkOf(neighbour)))
                            owner->Active[RegionIndex(neighbour)] = 1;
                    }
            }
        }
    }
    if (Root)
        Root->RefreshAll();
}

void WorldMap::Fill(WorldRect region, CellState cell) {
    ValidateRect(region);
    RequireResident(region);
    if (!cell.Force.IsValid())
        throw std::invalid_argument("invalid fill direction");
    for (auto &[coordinate, chunk] : Chunks) {
        const WorldRect chunkBounds{coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge,
                                    storage::ChunkEdge, storage::ChunkEdge};
        auto overlap = Intersection(region, chunkBounds);
        if (Finite)
            overlap = Intersection(overlap, Extent);
        if (overlap.Empty())
            continue;
        for (uint32_t row = 0; row < overlap.Height; ++row) {
            const auto index = Index({overlap.X, overlap.Y + row});
            std::fill_n(chunk->Data().Materials.begin() + index, overlap.Width, cell.Type);
            std::fill_n(chunk->Data().Directions.begin() + index, overlap.Width, PackedDirection{int8_t(cell.Force.X), int8_t(cell.Force.Y)});
            std::fill_n(chunk->Data().BaseForces.begin() + index, overlap.Width, ForceVector{3600.f * cell.Force.X, 3600.f * cell.Force.Y});
        }
        chunk->Recount();
        chunk->Dirty = true;
        chunk->Revision = ++Version;
        chunk->RegionVersions.fill(Version);
        chunk->Active.fill(1);
    }
    RebuildIndex();
    Wake({region.X - 1, region.Y - 1, region.Width + 2, region.Height + 2});
}

void WorldMap::FillForce(WorldRect region, ForceVector force) {
    if (!std::isfinite(force.X) || !std::isfinite(force.Y))
        throw std::invalid_argument("force must be finite");
    ValidateRect(region);
    RequireResident(region);
    for (auto &[coordinate, chunk] : Chunks) {
        const WorldRect chunkBounds{coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge, storage::ChunkEdge, storage::ChunkEdge};
        auto overlap = Intersection(region, chunkBounds);
        if (Finite)
            overlap = Intersection(overlap, Extent);
        if (overlap.Empty())
            continue;
        for (uint32_t row = 0; row < overlap.Height; ++row)
            std::fill_n(chunk->Data().BaseForces.begin() + Index({overlap.X, overlap.Y + row}), overlap.Width, force);
        chunk->Dirty = true;
        chunk->Revision = ++Version;
        chunk->RegionVersions.fill(Version);
        chunk->Active.fill(1);
    }
    RebuildIndex();
}

void WorldMap::Wake(WorldRect region) {
    if (!Root || region.Empty())
        return;
    Root->Visit(region, [](Node &node) {
        node.Chunk->Active[LocalRegion({node.X, node.Y})] = 1;
    });
}

void WorldMap::FillDirection(WorldRect region, ForceDirection direction) {
    if (!direction.IsValid())
        throw std::invalid_argument("invalid direction fill");
    FillForce(region, {3600.f * direction.X, 3600.f * direction.Y});
    for (auto &[coordinate, chunk] : Chunks) {
        auto overlap = Intersection(region, {coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge,
                                             storage::ChunkEdge, storage::ChunkEdge});
        if (Finite)
            overlap = Intersection(overlap, Extent);
        for (uint32_t row = 0; row < overlap.Height; ++row)
            std::fill_n(chunk->Data().Directions.begin() + Index({overlap.X, overlap.Y + row}), overlap.Width,
                        PackedDirection{int8_t(direction.X), int8_t(direction.Y)});
    }
}

void WorldMap::WakeNeighbours(CellCoord cell) { Wake({cell.X - 1, cell.Y - 1, 3, 3}); }

void WorldMap::Invalidate(WorldRect region) {
    if (!Root)
        return;
    Root->Visit(region, [&](Node &node) {
        auto &chunk = *node.Chunk;
        chunk.Dirty = true;
        chunk.Revision = ++Version;
        chunk.RegionVersions[LocalRegion({node.X, node.Y})] = Version;
        chunk.Active[LocalRegion({node.X, node.Y})] = 1;
    });
}

void WorldMap::Clear() {
    Root.reset();
    Chunks.clear();
    ++Version;
    ++ResidencyVersion;
}

void WorldMap::GenerateTerrain(int64_t surface) {
    if (Finite && (surface < 0 || surface > Extent.Height))
        throw std::out_of_range("terrain surface is outside the world");
    Fill(Extent, {});
    if (surface < Extent.Y + Extent.Height)
        Fill({Extent.X, std::max(Extent.Y, surface), Extent.Width,
              uint32_t(Extent.Y + Extent.Height - std::max(Extent.Y, surface))},
             {CellTypes::Dirt});
}

uint64_t WorldMap::RegionVersion(CellCoord cell) const {
    const auto *chunk = FindChunk(ChunkOf(cell));
    return chunk ? chunk->RegionVersions[RegionIndex(cell)] : 0;
}

void WorldMap::ForEachRegion(WorldRect bounds, const std::function<void(WorldRect, uint64_t)> &visitor) const {
    if (!Root)
        return;
    Root->Visit(bounds, [&](Node &node) {
        auto region = WorldRect{node.X, node.Y, storage::RegionEdge, storage::RegionEdge};
        if (Finite)
            region = Intersection(region, Extent);
        if (!region.Empty())
            visitor(region, node.Version);
    });
}

axiom::Array<WorldRect> WorldMap::TakeActiveRegions(WorldRect bounds, WorldRect dynamicForces) {
    axiom::Array<WorldRect> regions;
    if (!Root)
        return regions;
    Root->Visit(bounds, [&](Node &node) {
        auto region = Intersection(bounds, {node.X, node.Y, storage::RegionEdge, storage::RegionEdge});
        if (Finite)
            region = Intersection(region, Extent);
        if (node.Materials && !region.Empty())
            regions.push_back(region);
        // The returned list is the current set; mutations wake the next set.
        node.Chunk->Active[LocalRegion({node.X, node.Y})] = 0; }, true, dynamicForces);
    return regions;
}

std::span<const CellTypeId> WorldMap::MaterialRow(CellCoord begin, uint32_t count) const {
    const auto *chunk = FindChunk(ChunkOf(begin));
    if (!chunk || count > storage::ChunkEdge - Index(begin) % storage::ChunkEdge)
        throw std::out_of_range("material row must fit one resident chunk");
    return {chunk->Data().Materials.data() + Index(begin), count};
}

void WorldMap::Insert(std::unique_ptr<WorldChunk> chunk) {
    const auto coordinate = chunk->Coordinate;
    ValidateCoordinate({coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge});
    if (Chunks.contains(coordinate))
        throw std::invalid_argument("chunk is already resident");
    chunk->RegionVersions.fill(++Version);
    chunk->Active.fill(1);
    Chunks.emplace(coordinate, std::move(chunk));
    ++ResidencyVersion;
    RebuildIndex();
    Wake({coordinate.X * storage::ChunkEdge - 1, coordinate.Y * storage::ChunkEdge - 1,
          storage::ChunkEdge + 2, storage::ChunkEdge + 2});
}

std::unique_ptr<WorldChunk> WorldMap::Remove(CellCoord coordinate) {
    const auto found = Chunks.find(coordinate);
    if (found == Chunks.end())
        return {};
    auto chunk = std::move(found->second);
    Chunks.erase(found);
    ++ResidencyVersion;
    RebuildIndex();
    ++Version;
    return chunk;
}

axiom::Array<CellCoord> WorldMap::ChunkCoordinates() const {
    axiom::Array<CellCoord> result;
    result.reserve(Chunks.size());
    for (const auto &[coordinate, chunk] : Chunks)
        result.push_back(coordinate);
    return result;
}

void WorldMap::RebuildIndex() {
    Root.reset();
    if (Chunks.empty())
        return;
    const auto first = Chunks.begin()->first;
    int64_t x = first.X * storage::ChunkEdge, y = first.Y * storage::ChunkEdge, edge = storage::ChunkEdge;
    for (const auto &[coordinate, chunk] : Chunks) {
        const CellCoord origin{coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge};
        while (origin.X < x || origin.Y < y || origin.X + storage::ChunkEdge > x + edge || origin.Y + storage::ChunkEdge > y + edge) {
            if (origin.X < x)
                x -= edge;
            if (origin.Y < y)
                y -= edge;
            edge *= 2;
        }
    }
    Root = std::make_unique<Node>(Node{x, y, edge});
    for (auto &[coordinate, chunk] : Chunks)
        for (uint32_t row = 0; row < storage::RegionsPerAxis; ++row)
            for (uint32_t column = 0; column < storage::RegionsPerAxis; ++column)
                Root->Add({coordinate.X * storage::ChunkEdge + column * storage::RegionEdge,
                           coordinate.Y * storage::ChunkEdge + row * storage::RegionEdge},
                          chunk.get());
}

void WorldMap::RequireResident(WorldRect region) const {
    if (region.Empty())
        return;
    const CellCoord first{region.X, region.Y}, last{region.X + region.Width - 1, region.Y + region.Height - 1};
    if (!Contains(first) || !Contains(last))
        throw std::out_of_range("edit rectangle exceeds world bounds");
    const auto begin = ChunkOf(first), end = ChunkOf(last);
    const uint64_t count = uint64_t(end.X - begin.X + 1) * uint64_t(end.Y - begin.Y + 1);
    if (count > Chunks.size())
        throw std::out_of_range("edit rectangle includes nonresident chunks");
    for (int64_t y = begin.Y; y <= end.Y; ++y)
        for (int64_t x = begin.X; x <= end.X; ++x)
            if (!FindChunk({x, y}))
                throw std::out_of_range("edit rectangle includes nonresident chunks");
}

performance::MemoryUsage WorldMap::GetMemoryUsage() const {
    performance::MemoryUsage usage{sizeof(*this)};
    for (const auto &[coordinate, chunk] : Chunks)
        usage.AddOwned(chunk->GetMemoryUsage());
    const uint64_t nodes = Root ? Root->Count() * sizeof(Node) : 0;
    const uint64_t indexBytes = nodes + Chunks.size() * (sizeof(CellCoord) + sizeof(std::unique_ptr<WorldChunk>) + 4 * sizeof(void *));
    usage.UsedHeapBytes += indexBytes;
    usage.ReservedHeapBytes += indexBytes;
    return usage;
}

performance::MemoryUsage WorldMap::GetForceMemoryUsage() const {
    const uint64_t bytes = Chunks.size() * storage::ChunkCells * sizeof(ForceVector);
    return {0, bytes, bytes};
}

std::optional<CellState> WorldMapView::TryAtLocal(uint32_t x, uint32_t y) const {
    if (x >= Width || y >= Height)
        return std::nullopt;
    const CellCoord cell{Origin.X + x, Origin.Y + y};
    return Map ? Map->TryGet(cell) : Snapshot ? Snapshot->TryGet(cell)
                                              : std::nullopt;
}

CellState WorldMapView::AtLocal(uint32_t x, uint32_t y) const {
    if (x >= Width || y >= Height)
        throw std::out_of_range("view coordinates outside rectangle");
    // Missing space is an obstacle for numerical geometry and collision, never air.
    return TryAtLocal(x, y).value_or(CellState{static_cast<CellTypeId>(UINT32_MAX)});
}

CellState WorldMapView::AtIndex(size_t index) const {
    if (index >= CellCount() || !Width)
        throw std::out_of_range("view index outside rectangle");
    return AtLocal(uint32_t(index % Width), uint32_t(index / Width));
}

uint64_t WorldMapView::RegionVersion(int64_t x, int64_t y) const {
    return Map ? Map->RegionVersion({x, y}) : Snapshot ? Snapshot->RegionVersion({x, y})
                                                       : 0;
}

uint64_t WorldMapView::Identity() const {
    return Map ? Map->GetIdentity() : Snapshot ? Snapshot->GetMapIdentity()
                                               : 0;
}

void WorldSnapshot::Update(const WorldMap &map, WorldRect bounds) {
    ValidateRect(bounds);
    if (MapIdentity != map.GetIdentity()) {
        Regions.clear();
        MapIdentity = map.GetIdentity();
    }
    Extent = bounds;
    MapExtent = map.IsFinite() ? std::optional{map.Bounds()} : std::nullopt;
    for (auto it = Regions.begin(); it != Regions.end();) {
        const WorldRect region{it->first.X * storage::RegionEdge, it->first.Y * storage::RegionEdge,
                               storage::RegionEdge, storage::RegionEdge};
        if (!region.Intersects(bounds) || !map.FindChunk(ChunkOf({region.X, region.Y})))
            it = Regions.erase(it);
        else
            ++it;
    }
    map.ForEachRegion(bounds, [&](WorldRect rect, uint64_t version) {
        const auto key = RegionOf({rect.X, rect.Y});
        auto &region = Regions[key];
        if (region.Version == version)
            return;
        region.Cells.resize(size_t(storage::RegionEdge) * storage::RegionEdge);
        const CellCoord origin{key.X * storage::RegionEdge, key.Y * storage::RegionEdge};
        const auto *chunk = map.FindChunk(ChunkOf(origin));
        for (uint32_t y = 0; y < storage::RegionEdge; ++y)
            for (uint32_t x = 0; x < storage::RegionEdge; ++x) {
                const auto index = LocalIndex({origin.X + x, origin.Y + y});
                const auto direction = chunk->Data().Directions[index];
                region.Cells[size_t(y) * storage::RegionEdge + x] = {chunk->Data().Materials[index], {direction.X, direction.Y}};
            }
        region.Version = version;
    });
}

std::optional<CellState> WorldSnapshot::TryGet(CellCoord cell) const {
    if (!Extent.Contains(cell) || (MapExtent && !MapExtent->Contains(cell)))
        return std::nullopt;
    const auto key = RegionOf(cell);
    const auto found = Regions.find(key);
    if (found == Regions.end())
        return std::nullopt;
    return found->second.Cells[size_t(cell.Y - key.Y * storage::RegionEdge) * storage::RegionEdge + size_t(cell.X - key.X * storage::RegionEdge)];
}

uint64_t WorldSnapshot::RegionVersion(CellCoord cell) const {
    const auto found = Regions.find(RegionOf(cell));
    return found == Regions.end() ? 0 : found->second.Version;
}

performance::MemoryUsage WorldSnapshot::GetMemoryUsage() const {
    performance::MemoryUsage usage{sizeof(*this)};
    for (const auto &[coordinate, region] : Regions) {
        usage.UsedHeapBytes += region.Cells.size() * sizeof(CellState) + sizeof(region) + sizeof(coordinate);
        usage.ReservedHeapBytes += region.Cells.capacity() * sizeof(CellState) + sizeof(region) + sizeof(coordinate);
    }
    return usage;
}
} // namespace pixel
