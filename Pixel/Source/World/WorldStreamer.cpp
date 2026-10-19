#include "WorldStreamer.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <stdexcept>

namespace pixel {
namespace {
constexpr uint32_t FileMagic = 0x50435841; // AXCP
constexpr uint32_t FileVersion = 1;
constexpr uint64_t ChunkReservation = storage::ChunkReservedBytes + sizeof(WorldChunk) + WorldStreamer::ChunkWorkingReserve;

std::filesystem::path ChunkFile(const std::filesystem::path &directory, CellCoord coordinate) {
    return directory / "chunks" / (std::to_string(coordinate.X) + "_" + std::to_string(coordinate.Y) + ".bin");
}

template <class T>
void Write(std::ostream &output, const T &value) {
    static_assert(std::is_trivially_copyable_v<T>);
    output.write(reinterpret_cast<const char *>(&value), sizeof(value));
}

template <class T>
void Read(std::istream &input, T &value) {
    static_assert(std::is_trivially_copyable_v<T>);
    input.read(reinterpret_cast<char *>(&value), sizeof(value));
}
} // namespace

WorldStreamer::WorldStreamer(WorldMap &map, WorldStreamSettings settings)
    : Map(map), Configuration(std::move(settings)) {
    if (Map.IsFinite() || Configuration.Directory.empty() || Configuration.BudgetBytes < 2 * ChunkReservation || Configuration.WindowEdge < storage::RegionEdge || Configuration.WindowEdge % storage::RegionEdge)
        throw std::invalid_argument("invalid streaming world configuration");
    std::filesystem::create_directories(Configuration.Directory / "chunks");
    Worker = std::jthread([this] { Run(); });
}

WorldStreamer::~WorldStreamer() {
    // Owning session explicitly saves, so destructor only joins outstanding I/O.
    {
        std::lock_guard lock(Mutex);
        Stopping = true;
    }
    Changed.notify_all();
    if (Worker.joinable())
        Worker.join();
}

axiom::Array<CellCoord> WorldStreamer::Coordinates(WorldRect bounds) {
    axiom::Array<CellCoord> result;
    if (bounds.Empty())
        return result;
    const auto first = ChunkOf({bounds.X, bounds.Y});
    const auto last = ChunkOf({bounds.X + bounds.Width - 1, bounds.Y + bounds.Height - 1});
    for (int64_t y = first.Y; y <= last.Y; ++y)
        for (int64_t x = first.X; x <= last.X; ++x)
            result.push_back({x, y});
    return result;
}

uint64_t WorldStreamer::RetainedBytes() const {
    // Keep the I/O and index headroom while resident too: moving a dirty chunk
    // into the worker queue must not itself require extra budget.
    return Map.GetMemoryUsage().ReservedBytes() + Map.ChunkCount() * ChunkWorkingReserve + InFlightBytes + ExternalBytes;
}

size_t WorldStreamer::PendingCount() const { return Pending.size(); }

ChunkResidency WorldStreamer::GetState(CellCoord coordinate) const {
    if (Map.FindChunk(coordinate))
        return ChunkResidency::Resident;
    const auto pending = Pending.find(coordinate);
    if (pending != Pending.end())
        return pending->second == Operation::Load ? ChunkResidency::Loading : ChunkResidency::Saving;
    return Failures.contains(coordinate) ? ChunkResidency::Failed : ChunkResidency::Missing;
}

void WorldStreamer::Enqueue(Work work) {
    Pending.emplace(work.Coordinate, work.Kind);
    InFlightBytes += ChunkReservation;
    {
        std::lock_guard lock(Mutex);
        Queue.push_back(std::move(work));
    }
    Changed.notify_all();
}

void WorldStreamer::Run() {
    for (;;) {
        Work work;
        {
            std::unique_lock lock(Mutex);
            Changed.wait(lock, [this] { return Stopping || !Queue.empty(); });
            if (Queue.empty() && Stopping)
                return;
            work = std::move(Queue.front());
            Queue.erase(Queue.begin());
        }
        try {
            if (work.Kind == Operation::Load)
                work.Chunk = ReadChunk(Configuration.Directory, work.Coordinate, Configuration.SurfaceY);
            else {
                WriteChunk(Configuration.Directory, *work.Chunk);
                work.Chunk->Dirty = false;
            }
        } catch (const std::exception &exception) {
            work.Error = exception.what();
        }
        {
            std::lock_guard lock(Mutex);
            Completed.push_back(std::move(work));
        }
        Changed.notify_all();
    }
}

void WorldStreamer::Poll(bool acceptLoads) {
    axiom::Array<Work> completed;
    {
        std::lock_guard lock(Mutex);
        if (acceptLoads) {
            completed.swap(Completed);
        } else {
            for (auto it = Completed.begin(); it != Completed.end();) {
                if (it->Kind == Operation::Save) {
                    completed.push_back(std::move(*it));
                    it = Completed.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    for (auto &work : completed) {
        Pending.erase(work.Coordinate);
        InFlightBytes -= ChunkReservation;
        if (!work.Error.empty()) {
            Error = work.Error;
            Failures[work.Coordinate] = work.Error;
        } else {
            Failures.erase(work.Coordinate);
        }
        if (work.Chunk && (work.Kind == Operation::Load || work.Retain || !work.Error.empty()) && !Map.FindChunk(work.Coordinate))
            Map.Insert(std::move(work.Chunk));
    }
}

bool WorldStreamer::MakeRoom(uint64_t bytes, const axiom::Map<CellCoord, uint8_t, CellCoordLess> &wanted) {
    if (RetainedBytes() + bytes <= Configuration.BudgetBytes)
        return true;
    for (const auto coordinate : Map.ChunkCoordinates()) {
        if (wanted.contains(coordinate) || Pending.contains(coordinate) || Failures.contains(coordinate))
            continue;
        auto *chunk = Map.FindChunk(coordinate);
        if (chunk->Dirty)
            Enqueue({Operation::Save, coordinate, Map.Remove(coordinate), {}});
        else
            Map.Remove(coordinate);
        if (RetainedBytes() + bytes <= Configuration.BudgetBytes)
            return true;
    }
    return RetainedBytes() + bytes <= Configuration.BudgetBytes;
}

void WorldStreamer::Update(WorldRect simulation, WorldRect visible) {
    Poll();
    axiom::Array<CellCoord> requests = Coordinates(simulation);
    const auto display = Coordinates(visible);
    requests.insert(requests.end(), display.begin(), display.end());
    axiom::Map<CellCoord, uint8_t, CellCoordLess> wanted;
    for (const auto coordinate : requests)
        wanted[coordinate] = 1;
    MakeRoom(0, wanted);

    for (const auto coordinate : requests) {
        if (Map.FindChunk(coordinate) || Pending.contains(coordinate) || Failures.contains(coordinate))
            continue;
        if (Pending.size() >= 2 || !MakeRoom(ChunkReservation, wanted))
            break;
        Enqueue({Operation::Load, coordinate, {}, {}});
    }
    // A single low-priority ring; it can never evict a required chunk.
    const auto required = Coordinates(simulation);
    if (!required.empty() && Pending.size() < 2) {
        const auto first = required.front(), last = required.back();
        for (int64_t y = first.Y - 1; y <= last.Y + 1 && Pending.size() < 2; ++y)
            for (int64_t x = first.X - 1; x <= last.X + 1 && Pending.size() < 2; ++x) {
                const CellCoord coordinate{x, y};
                if (!Map.FindChunk(coordinate) && !Pending.contains(coordinate) && !Failures.contains(coordinate) && RetainedBytes() + ChunkReservation <= Configuration.BudgetBytes)
                    Enqueue({Operation::Load, coordinate, {}, {}});
            }
    }
}

void WorldStreamer::LoadNow(WorldRect bounds) {
    Wait();
    axiom::Map<CellCoord, uint8_t, CellCoordLess> wanted;
    const auto coordinates = Coordinates(bounds);
    for (const auto coordinate : coordinates)
        wanted[coordinate] = 1;
    for (const auto coordinate : coordinates) {
        if (Map.FindChunk(coordinate))
            continue;
        if (!MakeRoom(ChunkReservation, wanted)) {
            Wait();
            if (!MakeRoom(ChunkReservation, wanted))
                throw std::runtime_error("world budget cannot fit required region");
        }
        Enqueue({Operation::Load, coordinate, {}, {}});
        Wait();
        if (!Map.FindChunk(coordinate))
            throw std::runtime_error(Error.empty() ? "required chunk failed to load" : Error);
    }
}

void WorldStreamer::Wait() {
    while (!Pending.empty()) {
        Poll();
        if (Pending.empty())
            break;
        std::unique_lock lock(Mutex);
        Changed.wait(lock, [this] { return !Completed.empty(); });
    }
}

void WorldStreamer::WaitForSaves() {
    const auto hasSaves = [this] {
        return std::any_of(Pending.begin(), Pending.end(), [](const auto &item) { return item.second == Operation::Save; });
    };
    while (hasSaves()) {
        Poll(false);
        if (!hasSaves())
            break;
        std::unique_lock lock(Mutex);
        Changed.wait(lock, [this] {
            return std::any_of(Completed.begin(), Completed.end(), [](const Work &work) { return work.Kind == Operation::Save; });
        });
    }
}

void WorldStreamer::Save() {
    Wait();
    for (const auto coordinate : Map.ChunkCoordinates()) {
        if (!Map.FindChunk(coordinate)->Dirty)
            continue;
        Enqueue({Operation::Save, coordinate, Map.Remove(coordinate), {}, true});
        Wait();
        if (Map.FindChunk(coordinate)->Dirty)
            throw std::runtime_error(Error.empty() ? "world save failed" : Error);
    }
}

void WorldStreamer::CancelOptionalLoads(const axiom::Map<CellCoord, uint8_t, CellCoordLess> &required) {
    std::lock_guard lock(Mutex);
    const auto discard = [&](auto &items) {
        for (auto it = items.begin(); it != items.end();) {
            if (it->Kind == Operation::Load && !required.contains(it->Coordinate)) {
                Pending.erase(it->Coordinate);
                InFlightBytes -= ChunkReservation;
                it = items.erase(it);
            } else {
                ++it;
            }
        }
    };
    discard(Queue);
    discard(Completed);
}

bool WorldStreamer::ReserveExternalBytes(uint64_t bytes, WorldRect simulation, bool acceptCompleted) {
    if (bytes > Configuration.BudgetBytes)
        return false;
    const auto previous = ExternalBytes;
    ExternalBytes = bytes;
    if (RetainedBytes() <= Configuration.BudgetBytes)
        return true;
    axiom::Map<CellCoord, uint8_t, CellCoordLess> required;
    for (const auto coordinate : Coordinates(simulation))
        required[coordinate] = 1;
    CancelOptionalLoads(required);
    if (acceptCompleted)
        Poll();
    if (MakeRoom(0, required))
        return true;
    ExternalBytes = previous;
    return false;
}

void WorldStreamer::WriteChunk(const std::filesystem::path &directory, const WorldChunk &chunk) {
    static_assert(std::endian::native == std::endian::little, "chunk format currently requires little endian");
    std::filesystem::create_directories(directory / "chunks");
    std::ofstream output(ChunkFile(directory, chunk.Coordinate), std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    Write(output, FileMagic);
    Write(output, FileVersion);
    Write(output, storage::ChunkEdge);
    Write(output, chunk.Coordinate.X);
    Write(output, chunk.Coordinate.Y);
    const auto &data = chunk.Data();
    output.write(reinterpret_cast<const char *>(data.Materials.data()), data.Materials.size() * sizeof(CellTypeId));
    output.write(reinterpret_cast<const char *>(data.Directions.data()), data.Directions.size() * sizeof(PackedDirection));
    output.write(reinterpret_cast<const char *>(data.BaseForces.data()), data.BaseForces.size() * sizeof(ForceVector));
    output.close();
}

std::unique_ptr<WorldChunk> WorldStreamer::ReadChunk(const std::filesystem::path &directory, CellCoord coordinate, int64_t surface) {
    const auto path = ChunkFile(directory, coordinate);
    auto chunk = std::make_unique<WorldChunk>(coordinate);
    if (!std::filesystem::exists(path)) {
        chunk->Initialize(surface);
        return chunk;
    }
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    uint32_t magic, version, edge;
    CellCoord stored;
    Read(input, magic);
    Read(input, version);
    Read(input, edge);
    Read(input, stored.X);
    Read(input, stored.Y);
    if (magic != FileMagic || version != FileVersion || edge != storage::ChunkEdge || stored != coordinate || std::filesystem::file_size(path) != 3 * sizeof(uint32_t) + 2 * sizeof(int64_t) + sizeof(ChunkPayload))
        throw std::runtime_error("incompatible or truncated chunk file: " + path.string());
    auto &data = chunk->Data();
    input.read(reinterpret_cast<char *>(data.Materials.data()), data.Materials.size() * sizeof(CellTypeId));
    input.read(reinterpret_cast<char *>(data.Directions.data()), data.Directions.size() * sizeof(PackedDirection));
    input.read(reinterpret_cast<char *>(data.BaseForces.data()), data.BaseForces.size() * sizeof(ForceVector));
    for (size_t i = 0; i < storage::ChunkCells; ++i) {
        const auto direction = data.Directions[i];
        const auto force = data.BaseForces[i];
        if (direction.X < -1 || direction.X > 1 || direction.Y < -1 || direction.Y > 1 || !std::isfinite(force.X) || !std::isfinite(force.Y))
            throw std::runtime_error("invalid chunk cell data: " + path.string());
    }
    chunk->Recount();
    return chunk;
}
} // namespace pixel
