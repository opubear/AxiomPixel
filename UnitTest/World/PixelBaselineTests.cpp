#include "World/World.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
void Check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
pixel::PixelWindSettings Uniform(pixel::WindVelocity velocity = {8, 0}) {
    pixel::PixelWindSettings settings;
    settings.Ambient = velocity;
    settings.GustSpeed = 0;
    settings.Relaxation = 0;
    settings.Viscosity = 0;
    return settings;
}
void ForceSources() {
    pixel::World world(8, 8);
    world.FillBaseForce({0, 0});
    world.SetBaseForce(2, 2, {3000, 0});
    world.AddBaseForce(2, 2, {600, 0});
    world.PlaceDirt(2, 2);
    world.Tick(1.f / 60);
    Check(world.GetCell(3, 2).Type == pixel::CellTypes::Dirt, "continuous base force did not drive material");
    Check(world.GetBaseForceField().Get(2, 2) == pixel::ForceVector{3600, 0}, "material transported base force");
    Check(world.GetBaseForceField().Get(3, 2) == pixel::ForceVector{}, "material overwrote destination force");
    world.RemoveDirt(3, 2);
    world.ConfigurePixelWind(8, Uniform({8, 0}));
    Check(world.GetForce(2, 2) == pixel::ForceVector{4080, 0}, "wind acceleration did not add to base force");
    world.DisableWind();
    Check(world.GetForce(2, 2) == pixel::ForceVector{3600, 0}, "disabling wind destroyed another source");
    pixel::WorldState published(8, 8);
    world.PublishState(published);
    Check(world.GetForce(2, 2) == pixel::ForceVector{3600, 0}, "publication changed force storage");
    try {
        world.AddBaseForce(2, 2, {std::numeric_limits<float>::infinity(), 0});
        Check(false, "nonfinite force accepted");
    } catch (const std::invalid_argument &) {
    }
    Check(world.GetForce(2, 2) == pixel::ForceVector{3600, 0}, "invalid edit destroyed force");
    world.GenerateTerrain(8);
    Check(world.GetForce(2, 2) == pixel::ForceVector{0, 3600}, "terrain reset retained custom force");
}
void UniformAndProjection() {
    pixel::PixelWindField wind;
    auto settings = Uniform({8, -3});
    wind.Reset(31, 19, settings);
    for (int i = 0; i < 8; ++i)
        wind.Step(1.f / 60);
    const auto velocity = wind.Sample(8.5f, 7.5f);
    Check(std::abs(velocity.X - 8) < 1e-6 && std::abs(velocity.Y + 3) < 1e-6, "uniform baseline flow changed");
    wind.AddImpulse(10, 10, 4, {15, 12});
    Check(wind.GetReport().Converged && wind.GetReport().RmsDivergence <= 1.01e-6, "impulse projection failed tolerance");
    wind.Step(1.f / 60);
    Check(wind.GetReport().Converged, "transport projection failed tolerance");
    Check(wind.Sample(-1, 2) == pixel::WindVelocity{}, "outside wind should be calm");
    const auto previous = wind.Sample(4, 5);
    settings.MaxPressureIterations = 0;
    try {
        wind.Reset(31, 19, settings);
        Check(false, "invalid pressure budget accepted");
    } catch (const std::invalid_argument &) {
    }
    Check(wind.Sample(4, 5) == previous, "invalid reset destroyed reference state");
    settings = Uniform();
    settings.MaxPressureIterations = 1;
    settings.DivergenceTolerance = 1e-10;
    wind.Reset(31, 19, settings);
    wind.AddImpulse(10, 10, 4, {15, 12});
    Check(!wind.GetReport().Converged, "iteration exhaustion falsely reported convergence");
}
void ThinWallsAndPockets() {
    pixel::World geometry(32, 24);
    geometry.Fill({pixel::CellTypes::Empty, {0, 0}});
    // A fully closed 1-pixel wall loop, then a 1-pixel opening.
    for (uint32_t y = 5; y <= 18; ++y) {
        geometry.PlaceDirt(8, y);
        geometry.PlaceDirt(23, y);
    }
    for (uint32_t x = 8; x <= 23; ++x) {
        geometry.PlaceDirt(x, 5);
        geometry.PlaceDirt(x, 18);
    }
    pixel::PixelWindField wind;
    wind.Reset(32, 24, Uniform());
    wind.SetObstacles(geometry.ReadState());
    Check(wind.GetReport().Converged, "disconnected fluid projection did not converge");
    for (int step = 0; step < 6; ++step)
        wind.Step(1.f / 60);
    for (uint32_t y = 5; y <= 18; ++y) {
        Check(wind.FaceX(8, y) == 0 && wind.FaceX(9, y) == 0, "one-pixel wall leaked on its faces");
        Check(wind.Sample(8.5f, float(y) + 0.5f) == pixel::WindVelocity{}, "solid sample is nonzero");
    }
    Check(std::abs(wind.Sample(15.5f, 12.5f).X) < 1e-4, "closed pocket kept through-flow");
    geometry.RemoveDirt(8, 11);
    geometry.RemoveDirt(23, 11);
    wind.SetObstacles(geometry.ReadState());
    wind.AddImpulse(5, 11.5f, 8, {20, 0});
    const auto centre = wind.Sample(7.5f, 10.5f);
    Check(std::abs(centre.X - .5 * (wind.FaceX(7, 10) + wind.FaceX(8, 10))) < 1e-5, "wall zero face was omitted from interpolation");
    Check(std::abs(wind.FaceX(8, 11)) > 0.01 && wind.GetReport().Converged, "one-pixel opening did not carry flux");
    geometry.PlaceDirt(8, 11);
    wind.SetObstacles(geometry.ReadState());
    Check(wind.FaceX(8, 11) == 0, "closing opening retained flux");
    geometry.Fill({pixel::CellTypes::Dirt, {0, 0}});
    wind.SetObstacles(geometry.ReadState());
    wind.Step(1.f / 60);
    Check(wind.GetReport().Converged && wind.GetReport().RmsDivergence == 0, "fully solid domain failed");
    geometry.RemoveDirt(4, 4);
    wind.SetObstacles(geometry.ReadState());
    wind.Step(1.f / 60);
    Check(wind.Sample(4.5f, 4.5f) == pixel::WindVelocity{}, "isolated air pixel generated velocity");
}
void WorldReferenceIntegration() {
    pixel::World a(12, 10), b(12, 10);
    for (auto *world : {&a, &b}) {
        world->Fill({pixel::CellTypes::Empty, {0, 0}});
        world->SetBaseForce(1, 1, {100, 200});
        world->ConfigurePixelWind(8, Uniform({8, 0}));
        world->PlaceDirt(6, 3);
    }
    a.Tick(1.f / 30);
    for (int i = 0; i < 4; ++i)
        b.Tick(1.f / 120);
    Check(a.GetPixelWind().IsSolid(6, 3), "world edit was not synchronized into reference geometry");
    Check(a.GetPixelWind().GetReport().Converged, "world reference step failed convergence");
    for (uint32_t y = 0; y < 8; ++y)
        for (uint32_t x = 0; x < 12; ++x)
            Check(a.GetPixelWind().Sample(x + .5f, y + .5f) == b.GetPixelWind().Sample(x + .5f, y + .5f), "reference fixed-step evolution depends on frame rate");
    a.RemoveDirt(6, 3);
    a.Tick(1.f / 60);
    Check(!a.GetPixelWind().IsSolid(6, 3), "removed obstacle persisted in reference");
    a.ConfigureWind(8);
    Check(!a.UsesPixelWind() && a.GetWind().IsEnabled(), "switching back left two wind sources enabled");
    Check(a.GetBaseForceField().Get(1, 1) == pixel::ForceVector{100, 200}, "solver switching changed base force");
}
void BackendConfigurationIsTransactional() {
    pixel::World world(12, 10);
    world.Fill({pixel::CellTypes::Empty, {0, 0}});
    world.ConfigurePixelWind(8, Uniform({8, 2}));
    const auto referenceForce = world.GetForce(3, 3);
    auto coarseSettings = pixel::WindSettings{};
    coarseSettings.Viscosity = -1;
    try {
        world.ConfigureWind(8, coarseSettings);
        Check(false, "invalid coarse replacement accepted");
    } catch (const std::invalid_argument &) {
    }
    Check(world.UsesPixelWind() && world.GetForce(3, 3) == referenceForce, "failed coarse replacement destroyed reference state");
    Check(!world.GetWind().IsEnabled(), "inactive coarse diagnostics are enabled");
    world.ConfigureWind(8);
    const auto coarseForce = world.GetForce(3, 3);
    auto referenceSettings = Uniform();
    referenceSettings.MaxPressureIterations = 0;
    try {
        world.ConfigurePixelWind(8, referenceSettings);
        Check(false, "invalid reference replacement accepted");
    } catch (const std::invalid_argument &) {
    }
    Check(!world.UsesPixelWind() && world.GetForce(3, 3) == coarseForce, "failed reference replacement destroyed coarse state");
    Check(!world.GetPixelWind().IsEnabled(), "inactive reference diagnostics are enabled");
    world.DisableWind();
    Check(world.GetWindEndRow() == 0 && world.GetForce(3, 3) == pixel::ForceVector{}, "disabled backend still contributes force");
    try {
        world.AddWindImpulse(3, 3, 2, {1, 0});
        Check(false, "disabled wind accepted an impulse");
    } catch (const std::invalid_argument &) {
    }
}
void AnalyticShearAndGradient() {
    constexpr uint32_t n = 24;
    constexpr float dt = .01f;
    auto settings = Uniform({0, 0});
    settings.Viscosity = 2;
    pixel::PixelWindField wind;
    wind.Reset(n, n, settings);
    std::vector<double> u(n * n), v(n * n);
    for (uint32_t y = 0; y < n; ++y)
        for (uint32_t x = 0; x < n; ++x)
            u[y * n + x] = 2 * std::sin(2 * std::numbers::pi * (y + .5) / n);
    wind.SetFaceVelocities(u, v);
    wind.Step(dt);
    const double decay = 1 - 4 * settings.Viscosity * dt * std::pow(std::sin(std::numbers::pi / n), 2);
    for (uint32_t y = 0; y < n; ++y)
        for (uint32_t x = 0; x < n; ++x)
            Check(std::abs(wind.FaceX(x, y) - u[y * n + x] * decay) < 1e-10, "viscous shear disagrees with Fourier solution");
    for (uint32_t y = 0; y < n; ++y)
        for (uint32_t x = 0; x < n; ++x)
            u[y * n + x] = 3 + 2 * std::sin(2 * std::numbers::pi * x / n);
    wind.SetFaceVelocities(u, v);
    for (uint32_t y = 0; y < n; ++y)
        for (uint32_t x = 0; x < n; ++x)
            Check(std::abs(wind.FaceX(x, y) - 3) < 1e-8, "projection failed to remove pure gradient or changed uniform mode");
    wind.Reset(1, 1, Uniform({2, 3}));
    wind.Step(dt);
    Check(wind.Sample(.5f, .5f) == pixel::WindVelocity{2, 3}, "single-pixel periodic domain failed");
}
void TimeRefinement() {
    auto settings = Uniform({0, 0});
    settings.Viscosity = 2;
    // Check step refinement of a divergence-free shear pair.
    settings.GustSpeed = 2;
    pixel::PixelWindField coarse, fine, finer;
    coarse.Reset(24, 24, settings);
    fine.Reset(24, 24, settings);
    finer.Reset(24, 24, settings);
    coarse.Step(0.04f);
    for (int i = 0; i < 2; ++i)
        fine.Step(0.02f);
    for (int i = 0; i < 4; ++i)
        finer.Step(0.01f);
    double error1 = 0, error2 = 0;
    for (uint32_t y = 0; y < 24; ++y)
        for (uint32_t x = 0; x < 24; ++x) {
            const auto a = coarse.Sample(x + .5f, y + .5f), b = fine.Sample(x + .5f, y + .5f), c = finer.Sample(x + .5f, y + .5f);
            error1 += std::pow(a.X - c.X, 2) + std::pow(a.Y - c.Y, 2);
            error2 += std::pow(b.X - c.X, 2) + std::pow(b.Y - c.Y, 2);
        }
    Check(error2 < error1 * .6 && finer.GetReport().Converged, "time refinement did not reduce solution difference");
}
} // namespace
int main() {
    try {
        ForceSources();
        UniformAndProjection();
        ThinWallsAndPockets();
        WorldReferenceIntegration();
        BackendConfigurationIsTransactional();
        AnalyticShearAndGradient();
        TimeRefinement();
        std::cout << "Pixel force and reference wind tests passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
