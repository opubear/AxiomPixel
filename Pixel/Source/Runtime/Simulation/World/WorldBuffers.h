#pragma once

#include "WorldMap.h"
#include "Environment/ForceField.h"
#include "Environment/FieldSources.h"
#include "Environment/Wind/WindSimulation.h"
#include <array>

namespace pixel {
class World;
class Environment;

// One complete world version. Consumers only receive shared_ptr<const WorldBuffer>.
// Chunk payloads and numerical wind storage are shared until the writer changes them.
class WorldBuffer {
  public:
    WorldMapView Read(WorldRect bounds) const { return Map.View(bounds); }
    const WorldMap &GetMap() const { return Map; }
    const WindSimulation &GetWind() const { return Wind; }
    const FieldSources &GetSources() const { return Sources; }
    WorldRect SimulationBounds() const { return Simulation; }
    WorldRect WindBounds() const { return WindRegion; }
    bool IsWindEnabled() const { return WindEnabled; }
    WindVelocity SampleWind(CellCoord cell) const { return Wind.SampleWorld(cell); }
    ForceVector SampleForce(CellCoord cell) const;
    WindVelocity FieldVelocity(CellCoord cell) const;
    // Compose a value from an already validated, borrowed base-force row.
    WindVelocity FieldVelocity(CellCoord cell, ForceVector base) const;

  private:
    friend class WorldBuffers;
    friend class World;
    friend class Environment;
    WorldBuffer();
    WorldBuffer(uint32_t width, uint32_t height);
    void ShareFrom(const WorldBuffer &source);
    ForceVector SampleForce(CellCoord cell, ForceVector base) const;
    WorldMap Map;
    ForceField BaseForces;
    FieldSources Sources;
    WindSimulation Wind;
    WorldRect Simulation, WindRegion;
    bool WindEnabled = false;
};

// Simulation owns the only writable slot. Publish seals it; the next mutation
// prepares the other slot. A retained reader pins its version, so that slot is
// replaced rather than overwritten. Readers never participate in simulation.
class WorldBuffers {
  public:
    using ReadLease = std::shared_ptr<const WorldBuffer>;
    WorldBuffers();
    WorldBuffers(uint32_t width, uint32_t height);
    const WorldBuffer &Current() const { return *Slots[Writer]; }
    ReadLease Published() const { return Committed; }
    performance::MemoryUsage RetainedStorage(bool includeWind = true) const;
    uint64_t PublishedVersion() const { return Version; }

  private:
    friend class World;
    friend class Environment;
    WorldBuffer &Write();
    void DiscardPublished();
    ReadLease Publish(WorldRect simulation, WorldRect wind, bool enabled);
    std::array<std::shared_ptr<WorldBuffer>, 2> Slots;
    std::vector<std::weak_ptr<const WorldBuffer>> Retired;
    ReadLease Committed;
    std::function<void(uint64_t, std::span<const CellCoord>)> ReserveWrite;
    std::function<void(uint64_t)> ReserveVersion;
    void SetWriteReservation(std::function<void(uint64_t, std::span<const CellCoord>)> reserve,
                             std::function<void(uint64_t)> reserveVersion);
    size_t Writer = 0;
    bool Sealed = false;
    uint64_t Version = 0;
};
} // namespace pixel
