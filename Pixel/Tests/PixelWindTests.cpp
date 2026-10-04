#include "World/World.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Action>
void Reject(Action action) {
    try {
        action();
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("invalid wind input was accepted");
}
pixel::WindSettings Uniform(pixel::WindVelocity velocity = {}) {
    pixel::WindSettings settings;
    settings.Columns = 16;
    settings.Rows = 16;
    settings.Ambient = velocity;
    settings.GustSpeed = 0;
    settings.Relaxation = 0;
    return settings;
}
std::vector<pixel::WindVelocity> Samples(const pixel::WindField &wind, int width, int height) {
    std::vector<pixel::WindVelocity> samples;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            samples.push_back(wind.Sample(float(x) + 0.5f, float(y) + 0.5f));
    return samples;
}
double Energy(const pixel::WindField &wind, int width, int height) {
    double sum = 0;
    for (const auto v : Samples(wind, width, height)) {
        Check(std::isfinite(v.X) && std::isfinite(v.Y), "wind became nonfinite");
        sum += double(v.X) * v.X + double(v.Y) * v.Y;
    }
    return sum;
}
void UniformFlowAndValidation() {
    pixel::WindField wind;
    wind.Step(1);
    Check(wind.Sample(0, 0) == pixel::WindVelocity{} && wind.RmsDivergence() == 0, "disabled wind is not calm");
    Reject([&] { wind.AddImpulse(0, 0, 2, {1, 0}); });
    auto settings = Uniform({60, -20});
    wind.Reset(64, 32, settings);
    for (int i = 0; i < 60; ++i)
        wind.Step(1.f / 60);
    for (const auto v : Samples(wind, 64, 32))
        Check(std::abs(v.X - 60) < 0.002f && std::abs(v.Y + 20) < 0.002f, "uniform flow was not preserved");
    Check(wind.RmsDivergence() < 0.0001f, "uniform flow is divergent");
    const auto before = Samples(wind, 64, 32);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    for (float dt : {0.f, -1.f, nan, infinity})
        wind.Step(dt);
    Reject([&] { wind.Reset(0, 32, settings); });
    for (uint32_t columns : {0u, 1u, 129u, UINT32_MAX}) {
        auto invalid = settings;
        invalid.Columns = columns;
        Reject([&] { wind.Reset(64, 32, invalid); });
    }
    for (float value : {-1.f, nan, infinity}) {
        auto invalid = settings;
        invalid.Viscosity = value;
        Reject([&] { wind.Reset(64, 32, invalid); });
        invalid = settings;
        invalid.Relaxation = value;
        Reject([&] { wind.Reset(64, 32, invalid); });
        Reject([&] { wind.AddImpulse(10, 10, value, {20, 0}); });
    }
    Reject([&] { wind.AddImpulse(64, 0, 10, {20, 0}); });
    Reject([&] { wind.AddImpulse(0, 32, 10, {20, 0}); });
    Reject([&] { wind.AddImpulse(0, 0, 10, {nan, 0}); });
    Check(before == Samples(wind, 64, 32), "invalid wind operation mutated the field");
    for (auto position : {pixel::WindVelocity{-1, 1}, {64, 1}, {1, 32}, {nan, 1}, {1, infinity}})
        Check(wind.Sample(position.X, position.Y) == pixel::WindVelocity{}, "outside wind region was not calm");
    wind.Clear();
    Check(!wind.IsEnabled() && wind.Sample(1, 1) == pixel::WindVelocity{}, "wind clear retained velocity");
}

void ImpulseLocalityAtHighSpeed() {
    // A legal ambient + gust can exceed the per-impulse clamp. Sampling at MAC
    // faces isolates each component, including impulses crossing periodic seams.
    for (float sign : {-1.f, 1.f}) {
        auto settings = Uniform({sign * 1000, sign * 1000});
        settings.GustSpeed = 1000;
        pixel::WindField initial;
        initial.Reset(64, 64, settings);
        auto wind = initial;
        const auto before = Samples(initial, 64, 64);
        Check(std::any_of(before.begin(), before.end(), [](auto v) { return std::abs(v.X) > 1000 && std::abs(v.Y) > 1000; }),
              "high-speed fixture must exceed the impulse clamp in both components");
        wind.AddImpulse(0, 0, 8, {});
        Check(Samples(wind, 64, 64) == before, "zero impulse changed a high-speed field");
        for (auto delta : {pixel::WindVelocity{sign * 1000, 0}, pixel::WindVelocity{0, sign * 1000}}) {
            wind = initial;
            wind.AddImpulse(0, 0, 8, delta);
            bool changed = false;
            for (int row = 0; row < 16; ++row)
                for (int col = 0; col < 16; ++col)
                    for (int axis = 0; axis < 2; ++axis) {
                        const float x = (float(col) + (axis == 1 ? 0.5f : 0)) * 4;
                        const float y = (float(row) + (axis == 0 ? 0.5f : 0)) * 4;
                        const auto old = initial.Sample(x, y), now = wind.Sample(x, y);
                        const float oldComponent = axis == 0 ? old.X : old.Y;
                        const float newComponent = axis == 0 ? now.X : now.Y;
                        const float impulse = axis == 0 ? delta.X : delta.Y;
                        const float dx = std::min(x, 64 - x), dy = std::min(y, 64 - y);
                        if (impulse == 0 || dx * dx + dy * dy >= 64)
                            Check(newComponent == oldComponent, "impulse modified an unaffected face component");
                        else {
                            Check(std::abs(newComponent) <= 1000, "affected face exceeded the impulse limit");
                            changed |= newComponent != oldComponent;
                        }
                    }
            Check(changed, "nonzero local impulse had no effect");
        }
    }
}

void ProjectionDiffusionAndEvolution() {
    pixel::WindField wind;
    auto settings = Uniform();
    settings.Viscosity = 0;
    wind.Reset(64, 64, settings);
    wind.AddImpulse(32, 32, 14, {90, -30});
    const float divergence = wind.RmsDivergence();
    Check(divergence > 0.1f, "test impulse did not exercise pressure projection");
    wind.Step(1.f / 60);
    Check(wind.RmsDivergence() < divergence * 0.02f, "pressure projection did not sufficiently reduce divergence");

    pixel::WindField inviscid, viscous;
    inviscid.Reset(64, 64, settings);
    settings.Viscosity = 100;
    viscous.Reset(64, 64, settings);
    inviscid.AddImpulse(32, 32, 14, {90, -30});
    viscous.AddImpulse(32, 32, 14, {90, -30});
    for (int step = 0; step < 60; ++step) {
        inviscid.Step(1.f / 60);
        viscous.Step(1.f / 60);
    }
    Check(Energy(viscous, 64, 64) < Energy(inviscid, 64, 64) * 0.8, "viscosity did not dissipate the disturbance");

    settings = {};
    wind.Reset(1024, 424, settings);
    pixel::WindField same;
    same.Reset(1024, 424, settings);
    const auto initial = Samples(wind, 64, 64);
    for (int step = 0; step < 120; ++step) {
        wind.Step(1.f / 60);
        same.Step(1.f / 60);
    }
    Check(initial != Samples(wind, 64, 64), "default wind is static");
    Check(Samples(wind, 64, 64) == Samples(same, 64, 64), "seeded wind is not reproducible");
    Check(wind.RmsDivergence() < 0.02f && Energy(wind, 64, 64) > 0, "default wind lost stability");
    same.Reset(1024, 424, settings);
    ++settings.Seed;
    wind.Reset(1024, 424, settings);
    Check(Samples(wind, 64, 64) != Samples(same, 64, 64), "wind seed has no effect");

    // Periodic face sampling/forcing, including a pulse centred at the seam.
    wind.Reset(64, 64, Uniform());
    wind.AddImpulse(0, 32, 12, {60, 0});
    Check(wind.Sample(0.5f, 32).X > 0 && std::abs(wind.Sample(0.5f, 32).X - wind.Sample(63.5f, 32).X) < 0.001f,
          "impulse or sampling is discontinuous across the periodic seam");

    // Rectangular and tiny domains must not assume square cells or a large world.
    for (auto extent : {std::pair{1u, 1u}, {1u, 64u}, {64u, 1u}}) {
        wind.Reset(extent.first, extent.second);
        for (int i = 0; i < 20; ++i)
            wind.Step(1.f / 60);
        Check(std::isfinite(wind.RmsDivergence()) && std::isfinite(Energy(wind, extent.first, extent.second)), "small wind grid became unstable");
    }
}

void AdvectionTransportsShear() {
    auto settings = Uniform();
    settings.Viscosity = 0;
    pixel::WindField still, translating;
    still.Reset(64, 64, settings);
    settings.Ambient = {16, 0};
    translating.Reset(64, 64, settings);
    // Repeated vertical impulses create v(x), constant along Y. Its own
    // advection is zero, so only the added uniform horizontal wind transports it.
    for (int y = 0; y < 64; y += 4) {
        still.AddImpulse(32, float(y), 8, {0, 4});
        translating.AddImpulse(32, float(y), 8, {0, 4});
    }
    still.Step(0.25f);
    translating.Step(0.25f);
    // A uniform 16 cells/s flow translates the perturbation by four cells in
    // 0.25s (exactly one solver cell), in addition to the same self-advection.
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            const auto expected = still.Sample(float((x + 60) % 64) + 0.5f, float(y) + 0.5f);
            const auto actual = translating.Sample(float(x) + 0.5f, float(y) + 0.5f);
            Check(std::abs(actual.X - 16 - expected.X) < 0.01f && std::abs(actual.Y - expected.Y) < 0.01f,
                  "advection failed to transport the shear with the background flow");
        }
}

void WorldWindCoupling() {
    pixel::World world(16, 16);
    world.GenerateTerrain(12);
    world.ConfigureWind(8, Uniform({60, 0}));
    world.PlaceDirt(4, 4);
    world.PlaceDirt(4, 8);
    pixel::WorldState snapshot(16, 16);
    world.PublishState(snapshot);
    world.Tick(1.f / 60);
    Check(world.GetCell(5, 5).Type == pixel::CellTypes::Dirt, "wind did not combine with gravity");
    Check(world.GetCell(4, 9).Type == pixel::CellTypes::Dirt, "wind leaked into the stable sky band");
    Check(snapshot.Read().Cells[4 * 16 + 4].Type == pixel::CellTypes::Dirt && snapshot.Read().Cells[5 * 16 + 5].Type == pixel::CellTypes::Empty,
          "wind movement mutated the published snapshot");
    for (const auto cell : world.ReadState().Cells)
        Check(cell.Force == pixel::ForceDirection{0, 1}, "wind overwrote a base force");
    world.DisableWind();
    world.Tick(1.f / 60);
    Check(world.GetCell(5, 6).Type == pixel::CellTypes::Dirt, "disabling wind did not restore gravity");
    world.ConfigureWind(8, Uniform({60, 0}));
    world.GenerateTerrain(12);
    Check(!world.GetWind().IsEnabled(), "terrain regeneration retained old wind");
    world.ConfigureWind(8, Uniform({60, 0}));
    bool rejected = false;
    try {
        world.ConfigureWind(17);
    } catch (const std::out_of_range &) {
        rejected = true;
    }
    Check(rejected && world.GetWind().Sample(2, 2).X == 60, "invalid wind extent changed the world");

    // Wind must still obey the existing obstacle/chain/collision rules.
    world.GenerateTerrain(16);
    world.FillForceDirection({0, 0});
    world.ConfigureWind(16, Uniform({60, 0}));
    world.PlaceDirt(3, 3);
    world.PlaceDirt(4, 3);
    world.SetCell(5, 3, {{20}, {0, 0}});
    world.Tick(0.25f);
    Check(world.GetCell(3, 3).Type == pixel::CellTypes::Dirt && world.GetCell(4, 3).Type == pixel::CellTypes::Dirt, "wind pushed dirt through an obstacle");
    world.SetCell(5, 3, {pixel::CellTypes::Empty, {0, 0}});
    // The blocked stack now slows the wind. Restart the incident flow to test
    // the chain rule independently of its accumulated drag.
    world.ConfigureWind(16, Uniform({60, 0}));
    world.Tick(1.f / 60);
    Check(world.GetCell(4, 3).Type == pixel::CellTypes::Dirt && world.GetCell(5, 3).Type == pixel::CellTypes::Dirt, "wind broke movement chains");
}

void WorldWindTimingAndConservation() {
    pixel::World fast(64, 64), slow(64, 64);
    for (auto *world : {&fast, &slow}) {
        world->GenerateTerrain(56);
        world->ConfigureWind(48);
        for (uint32_t y = 2; y < 40; y += 3)
            for (uint32_t x = 2; x < 62; x += 3)
                world->PlaceDirt(x, y);
        world->AddWindImpulse(20, 20, 10, {-100, -100});
    }
    auto count = [](const pixel::World &world) {
        return std::count_if(world.ReadState().Cells.begin(), world.ReadState().Cells.end(), [](auto cell) { return cell.Type == pixel::CellTypes::Dirt; });
    };
    const auto initialCount = count(fast);
    for (int i = 0; i < 240; ++i)
        fast.Tick(1.f / 120);
    for (int i = 0; i < 60; ++i)
        slow.Tick(1.f / 30);
    Check(std::equal(fast.ReadState().Cells.begin(), fast.ReadState().Cells.end(), slow.ReadState().Cells.begin()), "wind-driven movement depends on frame rate");
    Check(Samples(fast.GetWind(), 64, 48) == Samples(slow.GetWind(), 64, 48), "wind solver depends on frame rate");
    Check(count(fast) == initialCount && count(slow) == initialCount, "wind movement created or lost material");
    fast.Tick(10);
    slow.Tick(0.25f);
    Check(Samples(fast.GetWind(), 64, 48) == Samples(slow.GetWind(), 64, 48), "long frame exceeded wind catch-up limit");
}

void WindDirectionAndDeadZone() {
    pixel::World world(8, 8);
    world.ConfigureWind(8, Uniform({0, -120}));
    world.PlaceDirt(3, 3);
    world.Tick(1.f / 60);
    Check(world.GetCell(3, 2).Type == pixel::CellTypes::Dirt, "updraft did not overcome gravity");
    world.ConfigureWind(8, Uniform({0, -60}));
    world.Tick(1.f / 60);
    Check(world.GetCell(3, 2).Type == pixel::CellTypes::Dirt, "opposing wind did not cancel gravity");
    world.FillForceDirection({0, 0});
    world.ConfigureWind(8, Uniform({29, -29}));
    world.Tick(1.f / 60);
    Check(world.GetCell(3, 2).Type == pixel::CellTypes::Dirt, "weak wind bypassed the dead zone");
    world.ConfigureWind(8, Uniform({-60, 60}));
    world.Tick(1.f / 60);
    Check(world.GetCell(2, 3).Type == pixel::CellTypes::Dirt, "wind did not handle negative X and positive Y");
}
} // namespace

void RunPixelWindTests() {
    UniformFlowAndValidation();
    ImpulseLocalityAtHighSpeed();
    ProjectionDiffusionAndEvolution();
    AdvectionTransportsShear();
    WorldWindCoupling();
    WorldWindTimingAndConservation();
    WindDirectionAndDeadZone();
}
