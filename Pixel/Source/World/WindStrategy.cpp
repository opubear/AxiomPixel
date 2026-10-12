#include "WindStrategy.h"
#include "LocalWindGeometry.h"
#include "ProceduralWindField.h"
#include <array>
#include <stdexcept>
#include <type_traits>

namespace pixel {
namespace {
template <class AirSample, class SolidTest>
WindVelocity SampleExposedAir(uint32_t width, uint32_t height, uint32_t x, uint32_t y, AirSample sample, SolidTest solid) {
    if (x >= width || y >= height)
        return {};
    if (!solid(x, y))
        return sample(float(x) + .5f, float(y) + .5f);
    const std::array<std::array<uint32_t, 2>, 4> neighbours{{{(x + width - 1) % width, y}, {(x + 1) % width, y}, {x, (y + height - 1) % height}, {x, (y + 1) % height}}};
    WindVelocity sum{};
    int count = 0;
    for (const auto &p : neighbours) {
        if (solid(p[0], p[1]))
            continue;
        const auto v = sample(float(p[0]) + .5f, float(p[1]) + .5f);
        sum.X += v.X;
        sum.Y += v.Y;
        ++count;
    }
    return count ? WindVelocity{sum.X / count, sum.Y / count} : WindVelocity{};
}
class CoarseWindStrategy final : public WindStrategy {
  public:
    CoarseWindStrategy(uint32_t width, uint32_t height, const WindSettings &settings) { Wind.Reset(width, height, settings); }
    WindStrategyKind Kind() const override { return WindStrategyKind::Coarse; }
    uint32_t GetEndRow() const override { return Wind.GetEndRow(); }
    bool NeedsMaterialMotion() const override { return true; }
    void AdvanceBeforeMaterials(WorldStateView, float seconds) override { Wind.Step(seconds); }
    void UpdateAfterMaterials(WorldStateView world, std::span<const DirtMotion> motion, float seconds) override { Wind.CoupleDirt(world, motion, seconds); }
    void AddImpulse(float x, float y, float radius, WindVelocity delta) override { Wind.AddImpulse(x, y, radius, delta); }
    WindVelocity Sample(float x, float y) const override { return Wind.Sample(x, y); }
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const override { return Sample(float(x) + .5f, float(y) + .5f); }
    const WindField *CoarseDiagnostics() const override { return &Wind; }

  private:
    WindField Wind;
};
class ReferenceWindStrategy final : public WindStrategy {
  public:
    ReferenceWindStrategy(WorldStateView world, uint32_t height, const PixelWindSettings &settings) {
        Wind.Reset(world.Width, height, settings);
        Wind.SetObstacles(world);
    }
    WindStrategyKind Kind() const override { return WindStrategyKind::Reference; }
    uint32_t GetEndRow() const override { return Wind.GetEndRow(); }
    void AdvanceBeforeMaterials(WorldStateView world, float seconds) override {
        Wind.SetObstacles(world);
        Wind.Step(seconds);
    }
    void UpdateAfterMaterials(WorldStateView world, std::span<const DirtMotion>, float) override { Wind.SetObstacles(world); }
    void AddImpulse(float x, float y, float radius, WindVelocity delta) override { Wind.AddImpulse(x, y, radius, delta); }
    WindVelocity Sample(float x, float y) const override { return Wind.Sample(x, y); }
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const override {
        return SampleExposedAir(Wind.GetWidth(), GetEndRow(), x, y, [this](float px, float py) { return Sample(px, py); }, [this](uint32_t px, uint32_t py) { return Wind.IsSolid(px, py); });
    }
    const PixelWindField *ReferenceDiagnostics() const override { return &Wind; }

  private:
    PixelWindField Wind;
};
// Both numerical prototypes use frozen impermeable geometry and exposed-air
// material sampling. This adapter shares that lifecycle, not their algorithms.
template <class Solver, WindStrategyKind kind>
class SolidWindStrategy final : public WindStrategy {
  public:
    template <class Settings>
    SolidWindStrategy(WorldStateView world, uint32_t height, const Settings &settings) : Wind(world, height, settings) {}
    WindStrategyKind Kind() const override { return kind; }
    uint32_t GetEndRow() const override { return Wind.GetEndRow(); }
    void AdvanceBeforeMaterials(WorldStateView world, float seconds) override {
        Wind.SetObstacles(world);
        Wind.Step(seconds);
    }
    void UpdateAfterMaterials(WorldStateView world, std::span<const DirtMotion>, float) override { Wind.SetObstacles(world); }
    void AddImpulse(float x, float y, float radius, WindVelocity delta) override { Wind.AddImpulse(x, y, radius, delta); }
    WindVelocity Sample(float x, float y) const override { return Wind.Sample(x, y); }
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const override {
        return SampleExposedAir(Wind.GetWidth(), GetEndRow(), x, y, [this](float px, float py) { return Sample(px, py); }, [this](uint32_t px, uint32_t py) { return Wind.IsSolid(px, py); });
    }
    const AdaptiveWindField *AdaptiveDiagnostics() const override {
        if constexpr (std::is_same_v<Solver, AdaptiveWindField>)
            return &Wind;
        else
            return nullptr;
    }
    const LbmWindField *LbmDiagnostics() const override {
        if constexpr (std::is_same_v<Solver, LbmWindField>)
            return &Wind;
        else
            return nullptr;
    }

  private:
    Solver Wind;
};
// Shared geometry response; derived strategies still own distinct time evolution
// and feedback rules. No force conversion belongs in this layer.
class LocalWindStrategy : public WindStrategy {
  public:
    uint32_t GetEndRow() const override { return Geometry.GetEndRow(); }
    WindVelocity Sample(float x, float y) const override { return Geometry.Constrain(x, y, SampleBackground(x, y)); }
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const override {
        return SampleExposedAir(Geometry.GetWidth(), GetEndRow(), x, y, [this](float px, float py) { return Sample(px, py); }, [this](uint32_t px, uint32_t py) { return Geometry.IsSolid(px, py); });
    }

  protected:
    LocalWindStrategy(WorldStateView world, uint32_t height, const LocalWindSettings &settings) : Geometry(world.Width, height, settings) { Geometry.Update(world); }
    virtual WindVelocity SampleBackground(float x, float y) const = 0;
    LocalWindGeometry Geometry;
};
class ProceduralWindStrategy final : public LocalWindStrategy {
  public:
    ProceduralWindStrategy(WorldStateView world, uint32_t height, const WindStrategySettings &settings)
        : LocalWindStrategy(world, height, settings.Local), Wind(world.Width, height, settings.Coarse) {}
    WindStrategyKind Kind() const override { return WindStrategyKind::Procedural; }
    void AdvanceBeforeMaterials(WorldStateView world, float seconds) override {
        Geometry.Update(world);
        Wind.Step(seconds);
    }
    void UpdateAfterMaterials(WorldStateView world, std::span<const DirtMotion>, float) override { Geometry.Update(world); }
    void AddImpulse(float x, float y, float radius, WindVelocity delta) override { Wind.AddImpulse(x, y, radius, delta); }

  private:
    WindVelocity SampleBackground(float x, float y) const override { return Wind.Sample(x, y); }
    ProceduralWindField Wind;
};
class HybridWindStrategy final : public LocalWindStrategy {
  public:
    HybridWindStrategy(WorldStateView world, uint32_t height, const WindStrategySettings &settings)
        : LocalWindStrategy(world, height, settings.Local) { Wind.Reset(world.Width, height, settings.Coarse); }
    WindStrategyKind Kind() const override { return WindStrategyKind::Hybrid; }
    bool NeedsMaterialMotion() const override { return true; }
    void AdvanceBeforeMaterials(WorldStateView world, float seconds) override {
        Geometry.Update(world);
        Wind.Step(seconds);
    }
    void UpdateAfterMaterials(WorldStateView world, std::span<const DirtMotion> motion, float seconds) override {
        // Only the coarse solver receives momentum feedback. Local shaping is a
        // sampling correction, never fed back as a second copy of dirt drag.
        Wind.CoupleDirt(world, motion, seconds);
        Geometry.Update(world);
    }
    void AddImpulse(float x, float y, float radius, WindVelocity delta) override { Wind.AddImpulse(x, y, radius, delta); }

  private:
    WindVelocity SampleBackground(float x, float y) const override { return Wind.Sample(x, y); }
    WindField Wind;
};
} // namespace
std::unique_ptr<WindStrategy> MakeWindStrategy(WorldStateView world, uint32_t endRow, const WindStrategySettings &settings) {
    if (!world.Width || !endRow || endRow > world.Height)
        throw std::out_of_range("wind boundary is outside the world");
    switch (settings.Kind) {
    case WindStrategyKind::Coarse:
        return std::make_unique<CoarseWindStrategy>(world.Width, endRow, settings.Coarse);
    case WindStrategyKind::Reference:
        return std::make_unique<ReferenceWindStrategy>(world, endRow, settings.Reference);
    case WindStrategyKind::Procedural:
        return std::make_unique<ProceduralWindStrategy>(world, endRow, settings);
    case WindStrategyKind::Adaptive:
        return std::make_unique<SolidWindStrategy<AdaptiveWindField, WindStrategyKind::Adaptive>>(world, endRow, settings.Adaptive);
    case WindStrategyKind::Lbm:
        return std::make_unique<SolidWindStrategy<LbmWindField, WindStrategyKind::Lbm>>(world, endRow, settings.Lbm);
    case WindStrategyKind::Hybrid:
        return std::make_unique<HybridWindStrategy>(world, endRow, settings);
    }
    throw std::invalid_argument("unknown wind strategy");
}
} // namespace pixel
