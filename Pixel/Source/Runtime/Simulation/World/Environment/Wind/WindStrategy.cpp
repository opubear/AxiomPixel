#include "WindStrategy.h"
#include "LocalWindGeometry.h"
#include "ProceduralWindField.h"
#include <array>
#include <stdexcept>
#include <type_traits>

namespace pixel {
namespace {
WindBoundaryMode SettingsBoundary(const AdaptiveWindSettings &settings) { return settings.Flow.Boundary; }
WindBoundaryMode SettingsBoundary(const LbmWindSettings &settings) { return settings.Boundary; }

template <class AirSample, class SolidTest>
WindVelocity SampleExposedAir(uint32_t width, uint32_t height, uint32_t x, uint32_t y,
                              WindBoundaryMode boundary, AirSample sample, SolidTest solid) {
    if (x >= width || y >= height)
        return {};
    if (!solid(x, y))
        return sample(float(x) + .5f, float(y) + .5f);
    WindVelocity sum{};
    int count = 0;
    for (const auto [dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
        int nx = int(x) + dx, ny = int(y) + dy;
        if (boundary == WindBoundaryMode::Periodic) {
            nx = (nx + int(width)) % int(width);
            ny = (ny + int(height)) % int(height);
        } else if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height)) {
            continue;
        }
        if (solid(uint32_t(nx), uint32_t(ny)))
            continue;
        const auto v = sample(float(nx) + .5f, float(ny) + .5f);
        sum.X += v.X;
        sum.Y += v.Y;
        ++count;
    }
    return count ? WindVelocity{sum.X / count, sum.Y / count} : WindVelocity{};
}
// Keep the configured region for comparison, without solver or geometry storage.
class EmptyWindStrategy final : public WindStrategy {
  public:
    std::shared_ptr<WindStrategy> Clone() const override { return std::make_shared<EmptyWindStrategy>(*this); }

    performance::MemoryUsage GetMemoryUsage() const override {
        return performance::MeasureObject(*this);
    }

    explicit EmptyWindStrategy(uint32_t endRow) : EndRow(endRow) {}
    WindStrategyKind Kind() const override { return WindStrategyKind::Empty; }
    uint32_t GetEndRow() const override { return EndRow; }
    void MoveDomain(WorldMapView, uint32_t height, int64_t, int64_t) override { EndRow = height; }
    void AdvanceBeforeMaterials(WorldMapView, float) override {}
    void UpdateAfterMaterials(WorldMapView, std::span<const DirtMotion>, float) override {}
    void AddImpulse(float, float, float, WindVelocity) override {}
    WindVelocity Sample(float, float) const override { return {}; }
    WindVelocity SampleForMaterial(uint32_t, uint32_t) const override { return {}; }

  private:
    uint32_t EndRow;
};

class CoarseWindStrategy final : public WindStrategy {
  public:
    std::shared_ptr<WindStrategy> Clone() const override { return std::make_shared<CoarseWindStrategy>(*this); }

    performance::MemoryUsage GetMemoryUsage() const override {
        auto usage = performance::MeasureObject(*this);
        usage.AddEmbedded(Wind.GetMemoryUsage());
        return usage;
    }

    CoarseWindStrategy(uint32_t width, uint32_t height, const WindSettings &settings) { Wind.Reset(width, height, settings); }
    WindStrategyKind Kind() const override { return WindStrategyKind::Coarse; }
    uint32_t GetEndRow() const override { return Wind.GetEndRow(); }
    bool NeedsMaterialMotion() const override { return true; }
    void MoveDomain(WorldMapView world, uint32_t height, int64_t dx, int64_t dy) override { Wind.MoveDomain(world.Width, height, dx, dy); }
    void AdvanceBeforeMaterials(WorldMapView, float seconds) override { Wind.Step(seconds); }
    void UpdateAfterMaterials(WorldMapView world, std::span<const DirtMotion> motion, float seconds) override { Wind.CoupleDirt(world, motion, seconds); }
    void AddImpulse(float x, float y, float radius, WindVelocity delta) override { Wind.AddImpulse(x, y, radius, delta); }
    WindVelocity Sample(float x, float y) const override { return Wind.Sample(x, y); }
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const override { return Sample(float(x) + .5f, float(y) + .5f); }
    const WindField *CoarseDiagnostics() const override { return &Wind; }

  private:
    WindField Wind;
};
class ReferenceWindStrategy final : public WindStrategy {
  public:
    std::shared_ptr<WindStrategy> Clone() const override { return std::make_shared<ReferenceWindStrategy>(*this); }

    performance::MemoryUsage GetMemoryUsage() const override {
        auto usage = performance::MeasureObject(*this);
        usage.AddEmbedded(Wind.GetMemoryUsage());
        return usage;
    }

    ReferenceWindStrategy(WorldMapView world, uint32_t height, const PixelWindSettings &settings) {
        Wind.Reset(world.Width, height, settings);
        Wind.SetObstacles(world);
    }
    WindStrategyKind Kind() const override { return WindStrategyKind::Reference; }
    uint32_t GetEndRow() const override { return Wind.GetEndRow(); }
    void MoveDomain(WorldMapView world, uint32_t height, int64_t dx, int64_t dy) override { Wind.MoveDomain(world, height, dx, dy); }
    void AdvanceBeforeMaterials(WorldMapView world, float seconds) override {
        Wind.SetObstacles(world);
        Wind.Step(seconds);
    }
    void UpdateAfterMaterials(WorldMapView world, std::span<const DirtMotion>, float) override { Wind.SetObstacles(world); }
    void AddImpulse(float x, float y, float radius, WindVelocity delta) override { Wind.AddImpulse(x, y, radius, delta); }
    WindVelocity Sample(float x, float y) const override { return Wind.Sample(x, y); }
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const override {
        return SampleExposedAir(Wind.GetWidth(), GetEndRow(), x, y, Wind.GetSettings().Boundary, [this](float px, float py) { return Sample(px, py); }, [this](uint32_t px, uint32_t py) { return Wind.IsSolid(px, py); });
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
    std::shared_ptr<WindStrategy> Clone() const override { return std::make_shared<SolidWindStrategy>(*this); }

    performance::MemoryUsage GetMemoryUsage() const override {
        auto usage = performance::MeasureObject(*this);
        usage.AddEmbedded(Wind.GetMemoryUsage());
        return usage;
    }

    template <class Settings>
    SolidWindStrategy(WorldMapView world, uint32_t height, const Settings &settings) : Wind(world, height, settings), Boundary(SettingsBoundary(settings)) {}
    WindStrategyKind Kind() const override { return kind; }
    uint32_t GetEndRow() const override { return Wind.GetEndRow(); }
    void MoveDomain(WorldMapView world, uint32_t height, int64_t dx, int64_t dy) override { Wind.MoveDomain(world, height, dx, dy); }
    void AdvanceBeforeMaterials(WorldMapView world, float seconds) override {
        Wind.SetObstacles(world);
        Wind.Step(seconds);
    }
    void UpdateAfterMaterials(WorldMapView world, std::span<const DirtMotion>, float) override { Wind.SetObstacles(world); }
    void AddImpulse(float x, float y, float radius, WindVelocity delta) override { Wind.AddImpulse(x, y, radius, delta); }
    WindVelocity Sample(float x, float y) const override { return Wind.Sample(x, y); }
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const override {
        return SampleExposedAir(Wind.GetWidth(), GetEndRow(), x, y, Boundary, [this](float px, float py) { return Sample(px, py); }, [this](uint32_t px, uint32_t py) { return Wind.IsSolid(px, py); });
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
    WindBoundaryMode Boundary;
};
// Shared geometry response; derived strategies still own distinct time evolution
// and feedback rules. No force conversion belongs in this layer.
class LocalWindStrategy : public WindStrategy {
  public:
    uint32_t GetEndRow() const override { return Geometry.GetEndRow(); }
    WindVelocity Sample(float x, float y) const override { return Geometry.Constrain(x, y, SampleBackground(x, y)); }
    WindVelocity SampleForMaterial(uint32_t x, uint32_t y) const override {
        return SampleExposedAir(Geometry.GetWidth(), GetEndRow(), x, y, Boundary, [this](float px, float py) { return Sample(px, py); }, [this](uint32_t px, uint32_t py) { return Geometry.IsSolid(px, py); });
    }

  protected:
    LocalWindStrategy(WorldMapView world, uint32_t height, const LocalWindSettings &settings, WindBoundaryMode boundary)
        : Geometry(world.Width, height, settings, boundary), GeometrySettings(settings), Boundary(boundary) { Geometry.Update(world); }
    LocalWindGeometry MakeGeometry(WorldMapView world, uint32_t height) const {
        LocalWindGeometry next(world.Width, height, GeometrySettings, Boundary);
        next.Update(world);
        return next;
    }
    virtual WindVelocity SampleBackground(float x, float y) const = 0;
    LocalWindGeometry Geometry;
    LocalWindSettings GeometrySettings;
    WindBoundaryMode Boundary;
};
class ProceduralWindStrategy final : public LocalWindStrategy {
  public:
    std::shared_ptr<WindStrategy> Clone() const override { return std::make_shared<ProceduralWindStrategy>(*this); }

    performance::MemoryUsage GetMemoryUsage() const override {
        auto usage = performance::MeasureObject(*this);
        usage.AddEmbedded(Geometry.GetMemoryUsage());
        usage.AddEmbedded(Wind.GetMemoryUsage());
        return usage;
    }

    ProceduralWindStrategy(WorldMapView world, uint32_t height, const WindStrategySettings &settings)
        : LocalWindStrategy(world, height, settings.Local, settings.Coarse.Boundary), Wind(world.Width, height, settings.Coarse) { Wind.SetOrigin(world.Origin); }
    WindStrategyKind Kind() const override { return WindStrategyKind::Procedural; }
    void MoveDomain(WorldMapView world, uint32_t height, int64_t, int64_t) override {
        auto next = MakeGeometry(world, height);
        Wind.MoveDomain(world.Width, height, world.Origin);
        Geometry = std::move(next);
    }
    void SetSimulationTime(double seconds) override { Wind.SetTime(seconds); }
    void AdvanceBeforeMaterials(WorldMapView world, float seconds) override {
        Geometry.Update(world);
        Wind.Step(seconds);
    }
    void UpdateAfterMaterials(WorldMapView world, std::span<const DirtMotion>, float) override { Geometry.Update(world); }
    void AddImpulse(float x, float y, float radius, WindVelocity delta) override { Wind.AddImpulse(x, y, radius, delta); }

  private:
    WindVelocity SampleBackground(float x, float y) const override { return Wind.Sample(x, y); }
    ProceduralWindField Wind;
};
class HybridWindStrategy final : public LocalWindStrategy {
  public:
    std::shared_ptr<WindStrategy> Clone() const override { return std::make_shared<HybridWindStrategy>(*this); }

    performance::MemoryUsage GetMemoryUsage() const override {
        auto usage = performance::MeasureObject(*this);
        usage.AddEmbedded(Geometry.GetMemoryUsage());
        usage.AddEmbedded(Wind.GetMemoryUsage());
        return usage;
    }

    HybridWindStrategy(WorldMapView world, uint32_t height, const WindStrategySettings &settings)
        : LocalWindStrategy(world, height, settings.Local, settings.Coarse.Boundary) { Wind.Reset(world.Width, height, settings.Coarse); }
    WindStrategyKind Kind() const override { return WindStrategyKind::Hybrid; }
    void MoveDomain(WorldMapView world, uint32_t height, int64_t dx, int64_t dy) override {
        auto next = MakeGeometry(world, height);
        Wind.MoveDomain(world.Width, height, dx, dy);
        Geometry = std::move(next);
    }
    bool NeedsMaterialMotion() const override { return true; }
    void AdvanceBeforeMaterials(WorldMapView world, float seconds) override {
        Geometry.Update(world);
        Wind.Step(seconds);
    }
    void UpdateAfterMaterials(WorldMapView world, std::span<const DirtMotion> motion, float seconds) override {
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
std::unique_ptr<WindStrategy> MakeWindStrategy(WorldMapView world, uint32_t endRow, const WindStrategySettings &settings) {
    if (!world.Width || !endRow || endRow > world.Height)
        throw std::out_of_range("wind boundary is outside the world");
    switch (settings.Kind) {
    case WindStrategyKind::Empty:
        return std::make_unique<EmptyWindStrategy>(endRow);
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
