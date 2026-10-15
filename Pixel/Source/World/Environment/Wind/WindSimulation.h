#pragma once

#include "WindStrategy.h"

namespace pixel {
// Transactional owner of one strategy. Numerical implementation and material
// coupling live in strategies, while Environment alone converts air into force.
class WindSimulation {
  public:
    performance::MemoryUsage GetMemoryUsage() const;

    void Configure(WorldStateView world, uint32_t endRow, const WindStrategySettings &settings);
    void ConfigureCoarse(uint32_t width, uint32_t endRow, const WindSettings &settings);
    void ConfigureReference(WorldStateView world, uint32_t endRow, const PixelWindSettings &settings);
    void Clear() { Backend.reset(); }
    WindStrategyKind GetKind() const { return Backend ? Backend->Kind() : WindStrategyKind::Empty; }
    bool UsesReference() const { return Backend && Backend->Kind() == WindStrategyKind::Reference; }
    bool NeedsMaterialMotion() const { return Backend && Backend->NeedsMaterialMotion(); }
    uint32_t GetEndRow() const { return Backend ? Backend->GetEndRow() : 0; }
    void AdvanceBeforeMaterials(WorldStateView world, float seconds);
    void UpdateAfterMaterials(WorldStateView world, std::span<const DirtMotion> motion, float seconds);
    void AddImpulse(float x, float y, float radius, WindVelocity delta);
    WindVelocity Sample(float x, float y) const { return Backend ? Backend->Sample(x, y) : WindVelocity{}; }
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const { return Backend ? Backend->SampleForMaterial(x, y) : WindVelocity{}; }
    // Inactive diagnostics are immutable empty objects. Do not retain references
    // across reconfiguration or Clear.
    const AdaptiveWindField *GetAdaptive() const { return Backend ? Backend->AdaptiveDiagnostics() : nullptr; }
    const LbmWindField *GetLbm() const { return Backend ? Backend->LbmDiagnostics() : nullptr; }
    const WindField &GetCoarse() const;
    const PixelWindField &GetReference() const;

  private:
    std::unique_ptr<WindStrategy> Backend;
};
} // namespace pixel
