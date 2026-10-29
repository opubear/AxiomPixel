#include "WorldStreamer.h"
#include "Runtime/System/IO/FileIO.h"
#include "WorldStorageError.h"
#include <algorithm>
#include <bit>
#include <fstream>
#include <stdexcept>

namespace pixel {
namespace {
constexpr uint32_t FileMagic = 0x50435841; // AXCP
constexpr uint32_t FileVersion = 1;
const uint64_t ChunkReservation = storage::ChunkReservedBytes() + sizeof(ChunkPayload) + sizeof(WorldChunk) + WorldStreamer::ChunkWorkingReserve;

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
    : WorldStreamer([&map]() -> const WorldMap & { return map; }, [&map]() -> WorldMap & { return map; }, std::move(settings)) {}

WorldStreamer::WorldStreamer(std::function<const WorldMap &()> read, std::function<WorldMap &()> write, WorldStreamSettings settings,
                             std::function<uint64_t()> retained)
    : ReadMap(std::move(read)), WriteMap(std::move(write)), BufferBytes(std::move(retained)), Configuration(std::move(settings)) {
    if (Configuration.Generation.Kind == WorldGeneratorKind::Flat)
        Configuration.Generation.SurfaceY = Configuration.SurfaceY;
    Generator = CreateWorldGenerator(Configuration.Generation);
    if (ReadMap().IsFinite() || Configuration.Directory.empty() || Configuration.BudgetBytes < 2 * LoadReservation() || Configuration.WindowEdge < storage::RegionEdge || Configuration.WindowEdge % storage::RegionEdge)
        throw std::invalid_argument("invalid streaming world configuration");
    std::filesystem::create_directories(Configuration.Directory / "chunks");
    Worker = std::jthread([this] { Run(); });
}

uint64_t WorldStreamer::LoadReservation() const {
    return ChunkReservation + WorldMap::ChunkIndexReservation() + Generator->WorkingBytes();
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
    return ReadMap().GetMemoryUsage().ReservedBytes() + ReadMap().ChunkCount() * ChunkWorkingReserve + InFlightBytes + ExternalBytes
        + (BufferBytes ? BufferBytes() : 0);
}

size_t WorldStreamer::PendingCount() const { return Pending.size(); }

ChunkResidency WorldStreamer::GetState(CellCoord coordinate) const {
    if (ReadMap().FindChunk(coordinate))
        return ChunkResidency::Resident;
    const auto pending = Pending.find(coordinate);
    if (pending != Pending.end())
        return pending->second == Operation::Load ? ChunkResidency::Loading : ChunkResidency::Saving;
    return Failures.contains(coordinate) ? ChunkResidency::Failed : ChunkResidency::Missing;
}

void WorldStreamer::Enqueue(Work work) {
    Pending.emplace(work.Coordinate, work.Kind);
    work.Reservation = work.Kind == Operation::Load ? LoadReservation() : ChunkReservation;
    InFlightBytes += work.Reservation;
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
        const auto failure = storage::CaptureFailure([&] {
            if (work.Kind == Operation::Load)
                work.Chunk = ReadChunk(Configuration.Directory, work.Coordinate, *Generator);
            else {
                WriteChunk(Configuration.Directory, *work.Chunk);
                work.Chunk->Dirty = false;
            }
        });
        if (failure) work.Error = *failure;
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
        // Prepare the writer before removing completed work from the queue. A
        // retained read version may prevent allocation; its I/O result must stay
        // queued so the next service boundary can retry without losing a chunk.
        for (const auto &work : Completed) {
            if (!acceptLoads && work.Kind != Operation::Save) continue;
            if (work.Chunk && (work.Kind == Operation::Load || work.Retain || !work.Error.empty())
                && !ReadMap().FindChunk(work.Coordinate)) {
                WriteMap();
                break;
            }
        }
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
        InFlightBytes -= work.Reservation;
        if (!work.Error.empty()) {
            Error = work.Error;
            Failures[work.Coordinate] = work.Error;
        } else {
            Failures.erase(work.Coordinate);
        }
        if (work.Chunk && (work.Kind == Operation::Load || work.Retain || !work.Error.empty()) && !ReadMap().FindChunk(work.Coordinate))
            WriteMap().Insert(std::move(work.Chunk));
    }
}

bool WorldStreamer::MakeRoom(uint64_t bytes, const axiom::Map<CellCoord, uint8_t, CellCoordLess> &wanted) {
    if (RetainedBytes() + bytes <= Configuration.BudgetBytes)
        return true;
    for (const auto coordinate : ReadMap().ChunkCoordinates()) {
        if (wanted.contains(coordinate) || Pending.contains(coordinate) || Failures.contains(coordinate))
            continue;
        auto *chunk = ReadMap().FindChunk(coordinate);
        if (chunk->Dirty)
            Enqueue({Operation::Save, coordinate, WriteMap().Remove(coordinate), {}});
        else
            WriteMap().Remove(coordinate);
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
        if (ReadMap().FindChunk(coordinate) || Pending.contains(coordinate) || Failures.contains(coordinate))
            continue;
        if (Pending.size() >= 2 || !MakeRoom(LoadReservation(), wanted))
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
                if (!ReadMap().FindChunk(coordinate) && !Pending.contains(coordinate) && !Failures.contains(coordinate) && RetainedBytes() + LoadReservation() <= Configuration.BudgetBytes)
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
        if (ReadMap().FindChunk(coordinate))
            continue;
        if (!MakeRoom(LoadReservation(), wanted)) {
            Wait();
            if (!MakeRoom(LoadReservation(), wanted))
                throw WorldStorageError("world budget cannot fit required region");
        }
        Enqueue({Operation::Load, coordinate, {}, {}});
        Wait();
        if (!ReadMap().FindChunk(coordinate))
            throw WorldStorageError(Error.empty() ? "required chunk failed to load" : Error);
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

void WorldStreamer::Save(bool acceptLoads) {
    if (acceptLoads) Wait();
    else WaitForSaves();
    for (const auto coordinate : ReadMap().ChunkCoordinates()) {
        if (!ReadMap().FindChunk(coordinate)->Dirty)
            continue;
        Enqueue({Operation::Save, coordinate, WriteMap().Remove(coordinate), {}, true});
        WaitForSaves();
        if (ReadMap().FindChunk(coordinate)->Dirty)
            throw WorldStorageError(Error.empty() ? "world save failed" : Error);
    }
}

void WorldStreamer::CancelOptionalLoads(const axiom::Map<CellCoord, uint8_t, CellCoordLess> &required) {
    std::lock_guard lock(Mutex);
    const auto discard = [&](auto &items) {
        for (auto it = items.begin(); it != items.end();) {
            if (it->Kind == Operation::Load && !required.contains(it->Coordinate)) {
                Pending.erase(it->Coordinate);
                InFlightBytes -= it->Reservation;
                it = items.erase(it);
            } else {
                ++it;
            }
        }
    };
    discard(Queue);
    discard(Completed);
}

bool WorldStreamer::ReserveExternalBytes(uint64_t bytes, WorldRect simulation, bool acceptCompleted, std::span<const CellCoord> protectedChunks) {
    if (bytes > Configuration.BudgetBytes)
        return false;
    const auto previous = ExternalBytes;
    ExternalBytes = bytes;
    if (RetainedBytes() <= Configuration.BudgetBytes)
        return true;
    axiom::Map<CellCoord, uint8_t, CellCoordLess> required;
    for (const auto coordinate : Coordinates(simulation))
        required[coordinate] = 1;
    for (const auto coordinate : protectedChunks)
        required[coordinate] = 1;
    CancelOptionalLoads(required);
    try {
        if (acceptCompleted)
            Poll();
        if (MakeRoom(0, required))
            return true;
    } catch (const WorldBudgetUnavailable &) {
        // A sealed map cannot be evicted until another version fits. Preserve
        // the bool reservation contract and restore the previous owner charge.
    }
    ExternalBytes = previous;
    return false;
}

void WorldStreamer::WriteChunk(const std::filesystem::path &directory, const WorldChunk &chunk) {
    static_assert(std::endian::native == std::endian::little, "chunk format currently requires little endian");
    std::filesystem::create_directories(directory / "chunks");
    const auto destination = ChunkFile(directory, chunk.Coordinate);
    io::CommitFile(destination, [&](std::ostream &output) {
        Write(output, FileMagic);
        Write(output, FileVersion);
        Write(output, storage::ChunkEdge);
        Write(output, chunk.Coordinate.X);
        Write(output, chunk.Coordinate.Y);
        const auto &data = chunk.Data();
        const auto writePlane = [&]<class T>(const memory::ChunkedGrid<T> &plane) {
            memory::PageBuffer<T> row(storage::ChunkEdge);
            for (uint32_t y = 0; y < storage::ChunkEdge; ++y) {
                plane.CopyRowTo(y, row);
                output.write(reinterpret_cast<const char *>(row.data()), row.size() * sizeof(T));
            }
        };
        writePlane(data.Materials);
        writePlane(data.Directions);
        writePlane(data.BaseForces);
    });
}

std::unique_ptr<WorldChunk> WorldStreamer::ReadChunk(const std::filesystem::path &directory, CellCoord coordinate, int64_t surface) {
    WorldGenerationSettings settings;
    settings.SurfaceY = surface;
    return ReadChunk(directory, coordinate, *CreateWorldGenerator(settings));
}

std::unique_ptr<WorldChunk> WorldStreamer::ReadChunk(const std::filesystem::path &directory, CellCoord coordinate, const IWorldGenerator &generator) {
    const auto path = ChunkFile(directory, coordinate);
    if (!std::filesystem::exists(path))
        return generator.GenerateChunk(coordinate);
    auto chunk = std::make_unique<WorldChunk>(coordinate);
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    uint32_t magic, version, edge;
    CellCoord stored;
    Read(input, magic);
    Read(input, version);
    Read(input, edge);
    Read(input, stored.X);
    Read(input, stored.Y);
    if (magic != FileMagic || version != FileVersion || edge != storage::ChunkEdge || stored != coordinate || std::filesystem::file_size(path) != 3 * sizeof(uint32_t) + 2 * sizeof(int64_t) + storage::ChunkPayloadBytes)
        throw WorldStorageError("incompatible or truncated chunk file: " + path.string());
    auto &data = chunk->Data();
    const auto readPlane = [&]<class T>(memory::ChunkedGrid<T> &plane) {
        memory::PageBuffer<T> row(storage::ChunkEdge);
        for (uint32_t y = 0; y < storage::ChunkEdge; ++y) {
            input.read(reinterpret_cast<char *>(row.data()), row.size() * sizeof(T));
            plane.CopyRowFrom(y, row);
        }
    };
    readPlane(data.Materials);
    readPlane(data.Directions);
    readPlane(data.BaseForces);
    for (size_t i = 0; i < storage::ChunkCells; ++i) {
        const auto direction = data.Directions[i];
        const auto force = data.BaseForces[i];
        if (!IsKnownCellType(data.Materials[i]) || direction.X < -1 || direction.X > 1 || direction.Y < -1 || direction.Y > 1 || !std::isfinite(force.X) || !std::isfinite(force.Y))
            throw WorldStorageError("invalid chunk cell data: " + path.string());
    }
    chunk->Recount();
    return chunk;
}
} // namespace pixel
