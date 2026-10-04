#pragma once

#include "Environment.h"
#include "WorldState.h"
#include <cstddef>

namespace pixel {
class World {
  public:
    World(uint32_t width, uint32_t height) : State(width, height) {}
    CellState GetCell(uint32_t x, uint32_t y) const;
    void SetCell(uint32_t x, uint32_t y, CellState cell);
    void Fill(CellState cell);
    // Field edits preserve material; placing/removing material preserves the field.
    void SetForceDirection(uint32_t x, uint32_t y, ForceDirection direction);
    void FillForceDirection(ForceDirection direction);
    // Randomize rows [0, endRow), counted from the top, not height above ground.
    // A supplied seed permits reproducible worlds.
    void RandomizeForceDirections(uint32_t endRow, uint32_t seed);
    // Optional dynamic wind adds to (and never overwrites) CellState::Force.
    void ConfigureWind(uint32_t endRow, const WindSettings &settings = {});
    void DisableWind() { Environment.GetWind().Clear(); }
    const WindField &GetWind() const { return Environment.GetWind(); }
    // Sample the same combined field used by movement at the cell centre.
    WindVelocity GetFieldVelocity(uint32_t x, uint32_t y) const;
    void AddWindImpulse(float x, float y, float radius, WindVelocity delta) { Environment.GetWind().AddImpulse(x, y, radius, delta); }
    void GenerateTerrain(uint32_t surfaceRow);
    bool PlaceDirt(uint32_t x, uint32_t y);
    bool RemoveDirt(uint32_t x, uint32_t y);
    void Tick(float duration);
    WorldStateView ReadState() const { return State.Read(); }

    // Publish into a distinct, same-sized CPU state, then retain the latest
    // contents in our recycled storage so incremental gameplay edits persist.
    void PublishState(WorldState &destination);

  private:
    WorldState State;
    pixel::Environment Environment;
    size_t CellIndex(uint32_t x, uint32_t y) const;
    void TickCharacters(float duration);
};
} // namespace pixel
