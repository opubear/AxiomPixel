#pragma once

#include "WorldMap.h"
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace pixel {
// A fixed step may retry after caches or publications release their storage.
class WorldBudgetUnavailable final : public std::runtime_error {
  public:
    WorldBudgetUnavailable() : std::runtime_error("world budget cannot fit material scratch storage") {}
};

struct WorldStreamSettings {
    std::filesystem::path Directory;
    uint64_t BudgetBytes = storage::DefaultBudget;
    uint32_t WindowEdge = storage::DefaultWindowEdge;
    int64_t SurfaceY = 512;
};

enum class ChunkResidency { Missing,
                            Loading,
                            Saving,
                            Resident,
                            Failed };

class WorldStreamer {
  public:
    static constexpr uint64_t ChunkWorkingReserve = 128 * 1024;
    WorldStreamer(WorldMap &map, WorldStreamSettings settings);
    ~WorldStreamer();
    WorldStreamer(const WorldStreamer &) = delete;
    WorldStreamer &operator=(const WorldStreamer &) = delete;

    void Update(WorldRect simulation, WorldRect visible);
    void Poll(bool acceptLoads = true);
    void LoadNow(WorldRect bounds);
    void Save();
    void Wait();
    void WaitForSaves();
    void SetExternalBytes(uint64_t bytes) { ExternalBytes = bytes; }
    bool ReserveExternalBytes(uint64_t bytes, WorldRect simulation, bool acceptCompleted = true);
    ChunkResidency GetState(CellCoord coordinate) const;
    void RetryFailures() {
        Failures.clear();
        Error.clear();
    }
    uint64_t RetainedBytes() const;
    performance::MemoryUsage GetMemoryUsage() const { return {sizeof(*this), InFlightBytes, InFlightBytes + Map.ChunkCount() * ChunkWorkingReserve}; }
    size_t PendingCount() const;
    const std::string &LastError() const { return Error; }
    const WorldStreamSettings &Settings() const { return Configuration; }
    static void WriteChunk(const std::filesystem::path &directory, const WorldChunk &chunk);
    static std::unique_ptr<WorldChunk> ReadChunk(const std::filesystem::path &directory, CellCoord coordinate, int64_t surface);

  private:
    enum class Operation { Load,
                           Save };
    struct Work {
        Operation Kind;
        CellCoord Coordinate;
        std::unique_ptr<WorldChunk> Chunk;
        std::string Error;
        bool Retain = false;
    };
    WorldMap &Map;
    WorldStreamSettings Configuration;
    mutable std::mutex Mutex;
    std::condition_variable Changed;
    axiom::Array<Work> Queue, Completed;
    axiom::Map<CellCoord, Operation, CellCoordLess> Pending;
    axiom::Map<CellCoord, std::string, CellCoordLess> Failures;
    std::jthread Worker;
    bool Stopping = false;
    uint64_t InFlightBytes = 0, ExternalBytes = 0;
    std::string Error;
    void Run();
    void Enqueue(Work work);
    bool MakeRoom(uint64_t bytes, const axiom::Map<CellCoord, uint8_t, CellCoordLess> &wanted);
    static axiom::Array<CellCoord> Coordinates(WorldRect bounds);
    void CancelOptionalLoads(const axiom::Map<CellCoord, uint8_t, CellCoordLess> &required);
};
} // namespace pixel
