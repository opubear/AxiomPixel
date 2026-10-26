#pragma once

#include "AdaptiveWindField.h"
#include "LbmWindField.h"
#include "PixelWindField.h"
#include "WindField.h"
#include <memory>

namespace pixel {
enum class WindStrategyKind { Coarse,
                              Reference,
                              Procedural,
                              Hybrid,
                              Adaptive,
                              Lbm,
                              Empty };
struct LocalWindSettings {
    float Radius = 6; // Pixel neighbourhood; bounds both work and influence.
    float Deflection = 0.7f;
    float WakeStrength = 0.65f;
};
struct WindStrategySettings {
    WindStrategyKind Kind = WindStrategyKind::Empty;
    WindSettings Coarse;
    PixelWindSettings Reference;
    LocalWindSettings Local;
    AdaptiveWindSettings Adaptive;
    LbmWindSettings Lbm;
};

// Air simulation only. No force storage, unit conversion or material movement.
// A strategy owns its geometry snapshot and all numerical state; world views are
// borrowed only during calls. Material response is separate from pure air sampling.
class WindStrategy {
  public:
    virtual ~WindStrategy() = default;

    // Includes the concrete adapter and every allocation retained by its solver.
    virtual performance::MemoryUsage GetMemoryUsage() const = 0;

    virtual std::shared_ptr<WindStrategy> Clone() const = 0;
    virtual WindStrategyKind Kind() const = 0;
    virtual uint32_t GetEndRow() const = 0;
    virtual void MoveDomain(WorldMapView world, uint32_t height, int64_t offsetX, int64_t offsetY) = 0;
    virtual void SetSimulationTime(double) {}
    virtual bool NeedsMaterialMotion() const { return false; }
    virtual void AdvanceBeforeMaterials(WorldMapView world, float seconds) = 0;
    virtual void UpdateAfterMaterials(WorldMapView world, std::span<const DirtMotion> motion, float seconds) = 0;
    virtual void AddImpulse(float x, float y, float radius, WindVelocity delta) = 0;
    virtual WindVelocity Sample(float x, float y) const = 0;
    virtual WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const = 0;
    virtual const WindField *CoarseDiagnostics() const { return nullptr; }
    virtual const AdaptiveWindField *AdaptiveDiagnostics() const { return nullptr; }
    virtual const LbmWindField *LbmDiagnostics() const { return nullptr; }
    virtual const PixelWindField *ReferenceDiagnostics() const { return nullptr; }
};
std::unique_ptr<WindStrategy> MakeWindStrategy(WorldMapView world, uint32_t endRow, const WindStrategySettings &settings);
} // namespace pixel
