#pragma once

#include "Runtime/Simulation/World/CellState.h"
#include "Runtime/Simulation/World/WorldCoordinates.h"
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace pixel {
class Environment;
class WorldMap;

// Handles belong to one World lifetime; reset never recycles an earlier ID.
struct FieldSourceHandle {
    uint64_t Id = 0;
    bool operator==(const FieldSourceHandle &) const = default;
};

struct FieldSourceSpec {
    WorldPosition Center;
    double Radius = 0;
    ForceVector Acceleration; // cells / second squared, independent of air velocity
    uint64_t Lifetime = 0;    // successful environment steps
};

struct FieldSourceSnapshot {
    FieldSourceHandle Handle;
    uint64_t Owner = 0;
    FieldSourceSpec Spec;
    uint64_t Remaining = 0;
    bool Suspended = false;
};
struct FieldSourcesState {
    uint64_t NextId = 1;
    std::vector<FieldSourceSnapshot> Sources;
};

// Environment owns all mutation. Stable ID order also defines floating-point
// addition order; removals preserve it and installation always appends.
class FieldSources {
  public:
    static constexpr size_t Capacity = 256;
    explicit FieldSources(const WorldMap &map) : Map(&map) {}
    size_t Size() const { return Count; }
    bool HasCapacity() const { return Count < Capacity; }
    uint64_t Revision() const;
    std::span<const FieldSourceSnapshot> Sources() const { return {Values.data(), Count}; }
    static bool Fits(const FieldSourceSpec &spec, WorldRect bounds);
    bool Contributes(FieldSourceHandle handle, WorldRect bounds) const;
    FieldSourcesState CaptureState() const { return {NextId, {Values.begin(), Values.begin() + Count}}; }

  private:
    friend class Environment;
    std::array<FieldSourceSnapshot, Capacity> Values{};
    size_t Count = 0;
    uint64_t NextId = 1;
    // Derived residency cache follows the same single-writer lifetime as World.
    // A center load/unload also invalidates force projections in neighbor chunks.
    const WorldMap *Map;
    mutable std::array<bool, Capacity> Resident{};
    mutable uint64_t Version = 1, CachedSourceVersion = 0, CachedResidencyVersion = 0;

    static FieldSourceSpec Validate(uint64_t owner, FieldSourceSpec spec);
    static WorldRect Bounds(const FieldSourceSpec &spec);
    FieldSourceSnapshot *Find(uint64_t owner, FieldSourceHandle handle);
    void RefreshResidency() const;
    bool Active(size_t index, WorldRect bounds) const;
    ForceVector Sample(CellCoord cell, WorldRect bounds) const;
    void WakeActive(WorldMap &map, WorldRect bounds) const;
    void CompleteStep(WorldMap &map, WorldRect bounds);
    void Remove(size_t index, WorldMap &map);
    void RestoreState(const FieldSourcesState &state, WorldMap &map);
};
} // namespace pixel
