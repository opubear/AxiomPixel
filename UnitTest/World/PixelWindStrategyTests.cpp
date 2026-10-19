#include "World/World.h"
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace pixel;
constexpr std::array Kinds{WindStrategyKind::Coarse, WindStrategyKind::Reference, WindStrategyKind::Procedural,
                           WindStrategyKind::Hybrid, WindStrategyKind::Adaptive, WindStrategyKind::Lbm};
void Check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
WindStrategySettings Uniform(WindStrategyKind kind) {
    WindStrategySettings s;
    s.Kind = kind;
    s.Coarse.Boundary = s.Reference.Boundary = s.Adaptive.Flow.Boundary = s.Lbm.Boundary = WindBoundaryMode::Periodic;
    s.Coarse.Ambient = s.Reference.Ambient = s.Adaptive.Flow.Ambient = s.Lbm.Ambient = {8, 0};
    s.Coarse.GustSpeed = s.Reference.GustSpeed = s.Adaptive.Flow.GustSpeed = s.Lbm.GustSpeed = 0;
    s.Coarse.Relaxation = s.Reference.Relaxation = s.Adaptive.Flow.Relaxation = s.Lbm.Relaxation = 0;
    return s;
}
void EmptyWindComparison() {
    World empty(32, 24), disabled(32, 24);
    empty.PlaceDirt(5, 5);
    disabled.PlaceDirt(5, 5);
    for (auto kind : Kinds) {
        empty.ConfigureWindStrategy(20, Uniform(kind));
        empty.ConfigureWindStrategy(20, WindStrategySettings{});
        const auto &wind = empty.GetEnvironment().GetWindSimulation();
        Check(wind.GetKind() == WindStrategyKind::Empty && wind.GetEndRow() == 20,
              "empty strategy lost selection or comparison region");
        Check(!wind.NeedsMaterialMotion() && !wind.GetCoarse().IsEnabled() &&
                  !wind.GetReference().IsEnabled() && !wind.GetAdaptive() && !wind.GetLbm(),
              "empty wind retained solver diagnostics or material feedback");
        empty.AddWindImpulse(5, 5, 8, {100, -100});
        empty.Tick(1.f / 60);
        disabled.Tick(1.f / 60);
        for (uint32_t y = 0; y < 24; ++y)
            for (uint32_t x = 0; x < 32; ++x) {
                Check(empty.SampleWind(x + .5f, y + .5f) == WindVelocity{}, "empty wind retained flow");
                Check(empty.GetForce(x, y) == disabled.GetForce(x, y), "empty wind changed base force");
                Check(empty.GetCell(x, y) == disabled.GetCell(x, y), "empty wind differs from disabled material movement");
            }
    }
}
void SwitchingAndForces() {
    World world(32, 24);
    world.FillBaseForce({123, 456});
    world.PlaceDirt(16, 12);
    for (const auto kind : Kinds) {
        auto settings = Uniform(kind);
        world.ConfigureWindStrategy(20, settings);
        Check(world.GetWindStrategyKind() == kind, "strategy not selected");
        Check(world.GetCell(16, 12).Type == CellTypes::Dirt, "switch destroyed materials");
        Check(world.GetBaseForceField().Get(16, 12) == ForceVector{123, 456}, "switch changed independent force field");
        const auto air = world.SampleWind(2.5f, 2.5f);
        const auto force = world.GetForce(2, 2);
        Check(std::abs(force.X - (123 + 60 * air.X)) < .001f && std::abs(force.Y - (456 + 60 * air.Y)) < .001f, "wind response is not separate and additive");
        Check(world.SampleWind(-1, 1) == WindVelocity{} && world.SampleWind(2, 20) == WindVelocity{}, "outside wind is not calm");
        const auto before = world.GetForce(2, 2);
        settings.Kind = WindStrategyKind(999);
        try {
            world.ConfigureWindStrategy(20, settings);
            Check(false, "unknown strategy accepted");
        } catch (const std::invalid_argument &) {
        }
        Check(world.GetWindStrategyKind() == kind && world.GetForce(2, 2) == before, "failed switch changed active backend");
        auto invalid = Uniform(kind);
        invalid.Coarse.GustSpeed = invalid.Reference.GustSpeed = invalid.Adaptive.Flow.GustSpeed = invalid.Lbm.GustSpeed = -1;
        try {
            world.ConfigureWindStrategy(20, invalid);
            Check(false, "invalid solver settings accepted");
        } catch (const std::invalid_argument &) {
        }
        Check(world.GetForce(2, 2) == before, "failed settings destroyed active state");
        try {
            world.ConfigureWindStrategy(25, Uniform(kind));
            Check(false, "invalid height accepted");
        } catch (const std::out_of_range &) {
        }
        world.DisableWind();
        Check(world.SampleWind(2, 2) == WindVelocity{} && world.GetForce(2, 2) == ForceVector{123, 456}, "disable retained wind or removed gravity");
    }
}
void DeterminismAndGeometry() {
    for (const auto kind : Kinds) {
        World a(24, 16), b(24, 16);
        for (auto *world : {&a, &b}) {
            world->FillBaseForce({});
            world->ConfigureWindStrategy(16, Uniform(kind));
            world->PlaceDirt(12, 8);
        }
        a.Tick(1.f / 30);
        for (int i = 0; i < 4; ++i)
            b.Tick(1.f / 120);
        for (uint32_t y = 0; y < 16; ++y)
            for (uint32_t x = 0; x < 24; ++x) {
                const auto av = a.SampleWind(x + .5f, y + .5f), bv = b.SampleWind(x + .5f, y + .5f);
                Check(std::isfinite(av.X) && std::isfinite(av.Y), "nonfinite wind");
                Check(av == bv, "fixed-step strategy depends on frame rate");
            }
        if (kind != WindStrategyKind::Coarse)
            Check(a.SampleWind(12.5f, 8.5f) == WindVelocity{}, "solid air sample is nonzero");
        a.RemoveDirt(12, 8);
        a.Tick(1.f / 60);
        a.AddWindImpulse(12.5f, 8.5f, 4, {8, 0});
        a.Tick(1.f / 60);
        Check(a.SampleWind(12.5f, 8.5f).X != 0, "removed solid persisted");
        const auto before = a.SampleWind(1.5f, 1.5f);
        a.AddWindImpulse(1, 1, 2, {});
        Check(a.SampleWind(1.5f, 1.5f) == before, "zero impulse changed the simulation");
        try {
            a.AddWindImpulse(1, 1, 0, {1, 0});
            Check(false, "invalid impulse accepted");
        } catch (const std::invalid_argument &) {
        }
        Check(a.SampleWind(1.5f, 1.5f) == before, "invalid impulse changed wind");
    }
}
void LocalModel() {
    for (auto kind : {WindStrategyKind::Procedural, WindStrategyKind::Hybrid}) {
        World world(48, 32);
        auto settings = Uniform(kind);
        settings.Coarse.DirtDrag = 0;
        world.PlaceDirt(24, 16);
        world.ConfigureWindStrategy(32, settings);
        const auto near = world.SampleWind(23.5f, 15.5f);
        Check(near.X < 8 && near.Y < 0, "local obstacle did not slow and deflect upstream air");
        Check(world.SampleWind(30.5f, 16.5f) == WindVelocity{8, 0}, "local model influence exceeded radius");
        Check(world.SampleWind(26.5f, 16.5f).X < 8, "downstream wake missing");
        world.RemoveDirt(24, 16);
        world.Tick(1.f / 60);
        Check(std::abs(world.SampleWind(23.5f, 15.5f).X - 8) < 1e-5, "removed local geometry retained wake source");
    }
    World a(24, 16), b(24, 16);
    WindStrategySettings s;
    s.Kind = WindStrategyKind::Procedural;
    a.ConfigureWindStrategy(16, s);
    b.ConfigureWindStrategy(16, s);
    const auto initial = a.SampleWind(4.5f, 4.5f);
    a.Tick(1.f / 60);
    b.Tick(1.f / 60);
    const auto next = a.SampleWind(4.5f, 4.5f);
    Check(next == b.SampleWind(4.5f, 4.5f) && next != initial, "procedural gusts not deterministic and evolving");
    Check(std::hypot(next.X - initial.X, next.Y - initial.Y) < 1, "procedural gust jumps in time");
}
void AdaptiveFlux() {
    World world(32, 24);
    world.FillBaseForce({});
    auto settings = Uniform(WindStrategyKind::Adaptive);
    world.ConfigureWindStrategy(24, settings);
    const auto emptyLeaves = world.GetAdaptiveWind()->GetLeafCount();
    Check(emptyLeaves == 12, "empty domain not coarsened");
    for (uint32_t y = 0; y < 24; ++y)
        world.PlaceDirt(15, y);
    world.ConfigureWindStrategy(24, settings);
    const auto *wind = world.GetAdaptiveWind();
    Check(wind->GetLeafCount() > emptyLeaves && wind->GetLeafCount() < 32 * 24, "obstacle did not trigger local refinement");
    Check(wind->GetReport().Converged && wind->GetReport().RmsDivergence <= 1.01e-6, "mixed-grid shared flux projection failed");
    Check(std::abs(world.SampleWind(12.5f, 12.5f).X) < 1e-4, "closed wall retained through flow");
    world.RemoveDirt(15, 12);
    world.Tick(1.f / 60);
    world.AddWindImpulse(13, 12.5f, 7, {20, 0});
    Check(world.GetAdaptiveWind()->GetReport().Converged, "opened adaptive channel projection failed");
    Check(world.SampleWind(15.5f, 12.5f).X > .01, "pixel-wide adaptive opening carries no flow");
    world.PlaceDirt(15, 12);
    world.Tick(1.f / 60);
    Check(world.SampleWind(15.5f, 12.5f) == WindVelocity{}, "closed adaptive opening still flows");
    world.Fill({CellTypes::Dirt});
    world.Tick(1.f / 60);
    Check(world.GetAdaptiveWind()->GetLeafCount() == 0 && world.GetAdaptiveWind()->GetReport().Converged, "all-solid adaptive domain failed");
}
void AdaptiveShearTransport() {
    World world(32, 32);
    AdaptiveWindSettings settings;
    settings.BlockSize = 4;
    settings.Flow.Boundary = WindBoundaryMode::Periodic;
    settings.Flow.GustSpeed = settings.Flow.Viscosity = settings.Flow.Relaxation = 0;
    settings.Flow.Ambient = {0, 8};
    AdaptiveWindField wind(world.ReadState(), 32, settings);
    std::vector<WindVelocity> initial(32 * 32);
    constexpr double tau = 6.283185307179586;
    for (uint32_t y = 0; y < 32; ++y)
        for (uint32_t x = 0; x < 32; ++x)
            initial[y * 32 + x] = {float(std::sin(tau * (y + .5) / 32)), 8};
    wind.SetVelocities(initial);
    // A divergence-free shear should travel down by 8 pixels in one second.
    // Upwind transport damps its amplitude, but the phase must advance.
    for (int i = 0; i < 60; ++i)
        wind.Step(1.f / 60);
    double sine = 0, cosine = 0;
    for (uint32_t y = 2; y < 32; y += 4) {
        const auto v = wind.Sample(2, float(y));
        sine += v.X * std::sin(tau * y / 32);
        cosine += v.X * std::cos(tau * y / 32);
        Check(std::abs(v.Y - 8) < 1e-5, "shear transport changed uniform transverse velocity");
    }
    Check(cosine < -1 && std::abs(sine) < std::abs(cosine) * .4, "adaptive shear did not advect by its expected phase");
    Check(wind.GetReport().Converged, "advected shear became divergent");
}
void LbmMassAndWalls() {
    World world(24, 16);
    auto settings = Uniform(WindStrategyKind::Lbm);
    world.ConfigureWindStrategy(16, settings);
    for (int i = 0; i < 30; ++i)
        world.Tick(1.f / 60);
    Check(std::abs(world.SampleWind(4.5f, 4.5f).X - 8) < 1e-4, "uniform LBM momentum changed");
    Check(std::abs(world.GetLbmWind()->GetReport().Mass - 384) < 1e-7, "periodic LBM lost mass");
    for (uint32_t y = 0; y < 16; ++y)
        world.PlaceDirt(12, y);
    world.ConfigureWindStrategy(16, settings);
    const double mass = world.GetLbmWind()->GetReport().Mass;
    // Pure solver steps keep wall geometry stationary while checking bounce-back.
    LbmWindField solver(world.ReadState(), 16, settings.Lbm);
    for (int i = 0; i < 40; ++i)
        solver.Step(1.f / 60);
    Check(std::abs(solver.GetReport().Mass - mass) < 1e-7, "static bounce-back lost mass");
    Check(solver.Sample(12.5f, 5.5f) == WindVelocity{}, "LBM wall leaked sampled air");
    Check(solver.GetReport().MinDensity > 0 && solver.GetReport().Stabilizations == 0, "low-speed LBM required recovery");
    solver.AddImpulse(5, 5, 3, {1000, 1000});
    Check(solver.GetReport().Stabilizations > 0, "LBM speed cap was not reported");
    world.Fill({CellTypes::Dirt});
    solver.SetObstacles(world.ReadState());
    solver.Step(1.f / 60);
    Check(solver.GetReport().Mass == 0 && solver.GetReport().MinDensity == 0, "all-solid LBM failed");
}
} // namespace
int main() {
    try {
        EmptyWindComparison();
        SwitchingAndForces();
        DeterminismAndGeometry();
        LocalModel();
        AdaptiveFlux();
        AdaptiveShearTransport();
        LbmMassAndWalls();
        std::cout << "All wind strategy tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
