#pragma once

#include "Environment.h"
#include "ForceField.h"
#include "WorldState.h"
#include <cstddef>

namespace pixel {
class World {
  public:
    World(uint32_t width, uint32_t height);
    CellState GetCell(uint32_t x, uint32_t y) const;
    void SetCell(uint32_t x, uint32_t y, CellState cell);
    void Fill(CellState cell);
    // Field edits preserve material; placing/removing material preserves the field.
    void SetForceDirection(uint32_t x, uint32_t y, ForceDirection direction);
    void FillForceDirection(ForceDirection direction);
    // Randomize rows [0, endRow), counted from the top, not height above ground.
    // A supplied seed permits reproducible worlds.
    void RandomizeForceDirections(uint32_t endRow, uint32_t seed);
    // Optional dynamic wind contributes to total acceleration without changing BaseForces.
    void ConfigureWind(uint32_t endRow, const WindSettings &settings = {});
    // Continuous spatial base acceleration; accepts gravity, explosions and other sources.
    // The legacy direction is metadata after these edits; movement reads BaseForces.
    void SetBaseForce(uint32_t x, uint32_t y, ForceVector force) { BaseForces.Set(x, y, force); }
    void AddBaseForce(uint32_t x, uint32_t y, ForceVector force) { BaseForces.Add(x, y, force); }
    void FillBaseForce(ForceVector force) { BaseForces.Fill(force); }
    const ForceField &GetBaseForceField() const { return BaseForces; }
    ForceVector GetForce(uint32_t x, uint32_t y) const;
    void ConfigurePixelWind(uint32_t endRow, const PixelWindSettings &settings = {});
    void ConfigureWindStrategy(uint32_t endRow, const WindStrategySettings &settings);
    WindStrategyKind GetWindStrategyKind() const { return Environment.GetWindSimulation().GetKind(); }
    // Pure air velocity, without gravity or the material surface response.
    WindVelocity SampleWind(float x, float y) const { return Environment.GetWindSimulation().Sample(x, y); }
    const AdaptiveWindField *GetAdaptiveWind() const { return Environment.GetWindSimulation().GetAdaptive(); }
    const LbmWindField *GetLbmWind() const { return Environment.GetWindSimulation().GetLbm(); }
    // Diagnostic references are valid only until wind is reconfigured or disabled.
    const PixelWindField &GetPixelWind() const { return Environment.GetWindSimulation().GetReference(); }
    bool UsesPixelWind() const { return Environment.GetWindSimulation().UsesReference(); }
    uint32_t GetWindEndRow() const { return Environment.GetWindSimulation().GetEndRow(); }
    void DisableWind() { Environment.GetWindSimulation().Clear(); }
    const WindField &GetWind() const { return Environment.GetWindSimulation().GetCoarse(); }
    // Sample the same combined field used by movement at the cell centre.
    WindVelocity GetFieldVelocity(uint32_t x, uint32_t y) const;
    void AddWindImpulse(float x, float y, float radius, WindVelocity delta) { Environment.GetWindSimulation().AddImpulse(x, y, radius, delta); }
    void GenerateTerrain(uint32_t surfaceRow);
    bool PlaceDirt(uint32_t x, uint32_t y);
    bool RemoveDirt(uint32_t x, uint32_t y);
    void Tick(float duration);
    WorldStateView ReadState() const { return State.Read(); }

    // Publish into a distinct, same-sized CPU state, then retain the latest
    // contents in our recycled storage so incremental gameplay edits persist.
    void PublishState(WorldState &destination);

  private:
    friend class Environment;
    void MoveMaterial(size_t source, size_t destination);
    WorldState State;
    ForceField BaseForces;
    pixel::Environment Environment;
    size_t CellIndex(uint32_t x, uint32_t y) const;
    void TickCharacters(float duration);
};
} // namespace pixel
