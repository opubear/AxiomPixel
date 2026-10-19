#pragma once

#include "Character.h"
#include "Environment/Environment.h"
#include "WorldStreamer.h"
#include <optional>

namespace pixel {
struct CellQuery {
    ChunkResidency Residency = ChunkResidency::Missing;
    std::optional<CellState> Cell;
};
enum class CellEditResult { Applied,
                            Blocked,
                            NotReady };

class World {
  public:
    World(uint32_t width, uint32_t height);
    World();
    performance::MemorySnapshot GetMemoryUsage() const;
    CellState GetCell(int64_t x, int64_t y) const { return Map.Get({x, y}); }
    std::optional<CellState> TryGetCell(int64_t x, int64_t y) const { return Map.TryGet({x, y}); }
    CellQuery QueryCell(int64_t x, int64_t y) const;
    void SetCell(int64_t x, int64_t y, CellState cell) { Map.Set({x, y}, cell); }
    void Fill(CellState cell);
    void FillRegion(WorldRect region, CellState cell) { Map.Fill(region, cell); }
    void SetForceDirection(int64_t x, int64_t y, ForceDirection direction);
    void FillForceDirection(ForceDirection direction);
    void RandomizeForceDirections(uint32_t endRow, uint32_t seed);
    void SetBaseForce(int64_t x, int64_t y, ForceVector force) { Map.SetForce({x, y}, force); }
    void AddBaseForce(int64_t x, int64_t y, ForceVector force);
    void FillBaseForce(ForceVector force);
    void FillBaseForceRegion(WorldRect region, ForceVector force) { Map.FillForce(region, force); }
    const ForceField &GetBaseForceField() const { return Environment.GetBaseForceField(); }
    ForceVector GetForce(int64_t x, int64_t y) const { return Environment.SampleForce({x, y}); }
    WindVelocity GetFieldVelocity(int64_t x, int64_t y) const;

    void ConfigureWind(uint32_t endRow, const WindSettings &settings = {});
    void ConfigurePixelWind(uint32_t endRow, const PixelWindSettings &settings = {});
    void ConfigureWindStrategy(uint32_t endRow, const WindStrategySettings &settings);
    void ConfigureWindDomain(int64_t boundaryY, const WindStrategySettings &settings, bool enabled = true);
    void SetWindBoundary(int64_t y);
    int64_t GetWindBoundary() const { return WindBoundaryY; }
    bool IsWindEnabled() const { return WindEnabled; }
    const WindStrategySettings &GetWindSettings() const { return WindSettings; }
    WindStrategyKind GetWindStrategyKind() const { return Environment.GetWindSimulation().GetKind(); }
    WindVelocity SampleWind(double x, double y) const { return Environment.GetWindSimulation().SampleWorld(x, y); }
    WindVelocity SampleWind(CellCoord cell, float offsetX = .5f, float offsetY = .5f) const {
        return Environment.GetWindSimulation().SampleWorld(cell, offsetX, offsetY);
    }
    const AdaptiveWindField *GetAdaptiveWind() const { return Environment.GetWindSimulation().GetAdaptive(); }
    const LbmWindField *GetLbmWind() const { return Environment.GetWindSimulation().GetLbm(); }
    const PixelWindField &GetPixelWind() const { return Environment.GetWindSimulation().GetReference(); }
    bool UsesPixelWind() const { return Environment.GetWindSimulation().UsesReference(); }
    uint32_t GetWindEndRow() const { return GetWindBounds().Height; }
    void DisableWind();
    const WindField &GetWind() const { return Environment.GetWindSimulation().GetCoarse(); }
    void AddWindImpulse(double x, double y, float radius, WindVelocity delta) {
        Environment.GetWindSimulation().AddWorldImpulse(x, y, radius, delta);
    }
    WorldRect GetWindBounds() const;
    WorldMapView GetWindView() const { return Map.View(GetWindBounds()); }

    void GenerateTerrain(uint32_t surfaceRow);
    bool PlaceDirt(int64_t x, int64_t y);
    bool RemoveDirt(int64_t x, int64_t y);
    CellEditResult TryPlaceDirt(int64_t x, int64_t y);
    CellEditResult TryRemoveDirt(int64_t x, int64_t y);
    void RetryStreaming() {
        if (Streamer)
            Streamer->RetryFailures();
    }
    void Tick(float duration);
    void SpawnPlayer(double x, double y);
    void RestorePlayer(WorldPosition position);
    const std::optional<Character> &GetPlayer() const { return Player; }
    void SetPlayerInput(int horizontal, bool jump);
    const pixel::Environment &GetEnvironment() const { return Environment; }
    const WorldMap &GetMap() const { return Map; }
    WorldMap &GetMutableMap() { return Map; }
    WorldMapView ReadState() const { return Map.View(GetSimulationBounds()); }
    void PublishState(WorldSnapshot &destination) const;
    void PublishSnapshot(WorldRect bounds, WorldSnapshot &destination) const;

    void EnableStreaming(WorldStreamSettings settings);
    void RequestVisibleRegion(WorldRect bounds);
    void UpdateStreaming();
    void Save();
    void WaitForStreaming();
    void LoadSimulationNow() {
        if (Streamer)
            Streamer->LoadNow(GetSimulationBounds());
    }
    void RestoreSimulationClock(double seconds, double remainder, bool deferredStep = false) { Environment.RestoreClock(seconds, remainder, deferredStep); }
    void SetWindSimulationTime(double seconds) { Environment.RestoreClock(seconds, 0); }
    void SetPublicationBytes(uint64_t bytes);
    bool ReservePublicationBytes(const void *owner, uint64_t bytes);
    void SetPublicationBytes(const void *owner, uint64_t bytes);
    void ReleasePublication(const void *owner);
    uint64_t PublicationBytesFor(const void *owner) const {
        const auto it = PublicationOwners.find(owner);
        return it == PublicationOwners.end() ? 0 : it->second;
    }
    void ReserveSimulationBytes(uint64_t bytes);
    uint64_t PublicationBudget() const { return Streamer ? Streamer->Settings().BudgetBytes / 3 : storage::DefaultBudget / 3; }
    std::weak_ptr<const int> PublicationLifetime() const { return Lifetime; }
    WorldRect GetSimulationBounds() const { return Map.IsFinite() ? Map.Bounds() : SimulationBounds; }
    const WorldStreamer *GetStreamer() const { return Streamer.get(); }

  private:
    WorldMap Map;
    pixel::Environment Environment;
    std::unique_ptr<WorldStreamer> Streamer;
    WorldRect SimulationBounds{0, 0, storage::DefaultWindowEdge, storage::DefaultWindowEdge};
    WorldRect VisibleBounds;
    int64_t WindBoundaryY = 424;
    bool WindEnabled = false;
    WindStrategySettings WindSettings;
    std::optional<Character> Player;
    int PlayerHorizontal = 0;
    bool PlayerJump = false;
    uint64_t PublicationBytes = 0;
    uint64_t ScratchReservation = 0;
    axiom::Map<const void *, uint64_t> PublicationOwners;
    std::shared_ptr<const int> Lifetime = std::make_shared<const int>(0);
    void RequireFinite() const;
};
} // namespace pixel
