#pragma once

#include "WindStrategy.h"

namespace pixel {
struct WindDomain {
    CellCoord Origin{};
    uint32_t Width = 0, Height = 0;
    bool operator==(const WindDomain &) const = default;
};

// Owns solver configuration and a movable numerical domain. Solvers see local
// coordinates; callers with world coordinates convert only at this boundary.
class WindSimulation {
  public:
    performance::MemoryUsage GetMemoryUsage() const;

    void Configure(WorldMapView world, uint32_t endRow, const WindStrategySettings &settings);
    void ConfigureCoarse(uint32_t width, uint32_t endRow, const WindSettings &settings);
    void ConfigureReference(WorldMapView world, uint32_t endRow, const PixelWindSettings &settings);
    void Clear();
    void MoveDomain(WorldMapView world, uint32_t endRow);
    const WindDomain &GetDomain() const { return Domain; }
    uint64_t GetRevision() const { return Revision; }
    void SetSimulationTime(double seconds);
    double GetSimulationTime() const { return SimulationTime; }
    WindStrategyKind GetKind() const { return Configured ? Settings.Kind : WindStrategyKind::Empty; }
    bool UsesReference() const { return GetKind() == WindStrategyKind::Reference; }
    bool NeedsMaterialMotion() const { return Backend && Backend->NeedsMaterialMotion(); }
    uint32_t GetEndRow() const { return Domain.Height; }
    void AdvanceBeforeMaterials(WorldMapView world, float seconds);
    void UpdateAfterMaterials(WorldMapView world, std::span<const DirtMotion> motion, float seconds);
    void AddImpulse(float x, float y, float radius, WindVelocity delta);
    void AddWorldImpulse(double x, double y, float radius, WindVelocity delta);
    bool AddWorldImpulse(WorldPosition position, float radius, WindVelocity delta);
    WindVelocity Sample(float x, float y) const { return Backend ? Backend->Sample(x, y) : WindVelocity{}; }
    WindVelocity SampleWorld(double x, double y) const;
    WindVelocity SampleWorld(CellCoord cell, float offsetX = .5f, float offsetY = .5f) const;
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const { return Backend ? Backend->SampleForMaterial(x, y) : WindVelocity{}; }
    WindVelocity SampleForMaterial(CellCoord position) const;
    // Inactive diagnostics are immutable empty objects. Do not retain references
    // across reconfiguration or domain movement.
    const AdaptiveWindField *GetAdaptive() const { return Backend ? Backend->AdaptiveDiagnostics() : nullptr; }
    const LbmWindField *GetLbm() const { return Backend ? Backend->LbmDiagnostics() : nullptr; }
    const WindField &GetCoarse() const;
    const PixelWindField &GetReference() const;

  private:
    std::unique_ptr<WindStrategy> Backend;
    WindStrategySettings Settings;
    WindDomain Domain;
    uint32_t ConfiguredEndRow = 0;
    bool Configured = false, FollowsView = false;
    double SimulationTime = 0;
    uint64_t Revision = 0;
};
} // namespace pixel
