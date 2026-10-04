#include "World/World.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
constexpr float Step = 1.f / 60;
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
pixel::WindSettings Uniform(pixel::WindVelocity velocity = {}) {
    pixel::WindSettings settings;
    settings.Columns = settings.Rows = 16;
    settings.Ambient = velocity;
    settings.GustSpeed = settings.Viscosity = settings.Relaxation = 0;
    return settings;
}
std::vector<pixel::WindVelocity> Samples(const pixel::WindField &wind, uint32_t width, uint32_t height) {
    std::vector<pixel::WindVelocity> result;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            result.push_back(wind.Sample(float(x) + 0.5f, float(y) + 0.5f));
    return result;
}
void CoverageAndDrag() {
    // Uniform full coverage has the analytic solution u(t)=u(0)*exp(-drag*t).
    // Include anisotropic cells and a wind grid finer than the material pixels.
    for (auto extent : {std::pair{64u, 32u}, {7u, 11u}, {1u, 1u}}) {
        pixel::World world(extent.first, extent.second);
        world.Fill({pixel::CellTypes::Dirt});
        auto settings = Uniform({20, -10});
        settings.Columns = 13;
        settings.Rows = 9;
        pixel::WindField wind;
        wind.Reset(extent.first, extent.second, settings);
        wind.CoupleDirt(world.ReadState(), {}, Step);
        const float retention = std::exp(-settings.DirtDrag * Step);
        for (auto v : Samples(wind, extent.first, extent.second))
            Check(std::abs(v.X - 20 * retention) < 0.0002f && std::abs(v.Y + 10 * retention) < 0.0002f,
                  "full dirt coverage depends on grid resolution or fails to damp wind");
        Check(wind.RmsDivergence() < 0.001f, "uniform dirt feedback introduced divergence");
    }
    pixel::World half(16, 16);
    for (uint32_t y = 0; y < 16; ++y)
        for (uint32_t x = 0; x < 16; x += 2)
            half.PlaceDirt(x, y);
    auto settings = Uniform({20, -10});
    settings.Columns = settings.Rows = 2;
    pixel::WindField wind;
    wind.Reset(16, 16, settings);
    wind.CoupleDirt(half.ReadState(), {}, Step);
    const auto velocity = wind.Sample(8, 8);
    Check(std::abs(velocity.X - 20 * std::exp(-1.f)) < 0.0002f
              && std::abs(velocity.Y + 10 * std::exp(-0.5f)) < 0.0002f,
          "vertical strips did not obstruct normal flow more than tangential flow");
    half.Fill({});
    for (uint32_t y = 0; y < 16; y += 2)
        for (uint32_t x = 0; x < 16; ++x)
            half.PlaceDirt(x, y);
    wind.Reset(16, 16, settings);
    wind.CoupleDirt(half.ReadState(), {}, Step);
    const auto rotated = wind.Sample(8, 8);
    Check(std::abs(rotated.X - 20 * std::exp(-0.5f)) < 0.0002f
              && std::abs(rotated.Y + 10 * std::exp(-1.f)) < 0.0002f,
          "projected obstacle area is not symmetric under axis rotation");
}

void NarrowStackInProductionWorld() {
    // The reported case: a supported, one-pixel-wide stack rises from terrain
    // into the default wind region. Keep the wind below the movement threshold
    // to isolate airflow around the stack rather than erosion of its shape.
    pixel::World pile(1024, 1024), empty(1024, 1024);
    pile.GenerateTerrain(512);
    empty.GenerateTerrain(512);
    for (uint32_t y = 344; y < 512; ++y)
        pile.PlaceDirt(512, y);
    pixel::WindSettings settings;
    settings.Ambient = {20, 0};
    settings.GustSpeed = settings.Viscosity = settings.Relaxation = 0;
    pile.ConfigureWind(424, settings);
    empty.ConfigureWind(424, settings);
    for (int i = 0; i < 60; ++i) {
        pile.Tick(Step);
        empty.Tick(Step);
    }
    const auto upstreamTop = pile.GetWind().Sample(496, 332);
    const auto upstreamSide = pile.GetWind().Sample(496, 360);
    Check(upstreamTop.X > 0 && upstreamTop.Y < -8,
          "air did not turn upward around the top of a narrow stack at production scale");
    Check(upstreamSide.X < empty.GetWind().Sample(496, 360).X * 0.4f,
          "a thin stack was diluted into a nearly transparent coarse cell");
    const auto field = pile.GetFieldVelocity(496, 332), reference = empty.GetFieldVelocity(496, 332);
    const double cross = double(field.X) * reference.Y - double(field.Y) * reference.X;
    const double dot = double(field.X) * reference.X + double(field.Y) * reference.Y;
    Check(std::abs(std::atan2(cross, dot)) > 0.035, "F1's combined field does not reflect the airflow around the stack");
    Check(pile.GetCell(512, 344).Type == pixel::CellTypes::Dirt && pile.GetWind().RmsDivergence() < 0.001f,
          "stack deflection fixture moved or lost incompressibility");
}

void StaticDirtDeflectsAir() {
    pixel::World obstacle(64, 64);
    for (uint32_t y = 24; y < 40; ++y)
        for (uint32_t x = 24; x < 40; ++x)
            obstacle.PlaceDirt(x, y);
    pixel::WindField wind, empty;
    wind.Reset(64, 64, Uniform({20, 0}));
    empty = wind;
    for (int i = 0; i < 60; ++i) {
        wind.Step(Step);
        wind.CoupleDirt(obstacle.ReadState(), {}, Step);
        empty.Step(Step);
    }
    Check(wind.Sample(32, 32).X < empty.Sample(32, 32).X * 0.2f, "stationary dirt did not resist wind");
    Check(std::abs(wind.Sample(24, 22).Y) > 0.1f, "dirt did not deflect surrounding air");
    Check(wind.RmsDivergence() < 0.02f, "dirt feedback was not pressure projected");
    for (auto v : Samples(wind, 64, 64))
        Check(std::isfinite(v.X) && std::isfinite(v.Y), "dirt feedback made the wind nonfinite");
}

void ActualMovementFeedback() {
    pixel::World falling(32, 32), blocked(32, 32), empty(32, 32);
    for (auto *world : {&falling, &blocked, &empty})
        world->ConfigureWind(32, Uniform());
    falling.PlaceDirt(16, 10);
    blocked.PlaceDirt(16, 31);
    pixel::WorldState snapshot(32, 32);
    falling.PublishState(snapshot);
    falling.Tick(Step);
    blocked.Tick(Step);
    empty.Tick(Step);
    Check(falling.GetCell(16, 11).Type == pixel::CellTypes::Dirt, "feedback broke gravity movement");
    Check(falling.GetWind().Sample(16.5f, 11.5f).Y > 0.1f, "falling dirt did not push still air down");
    Check(Samples(blocked.GetWind(), 32, 32) == Samples(empty.GetWind(), 32, 32), "blocked dirt used intended rather than actual velocity");
    Check(snapshot.Read().Cells[10 * 32 + 16].Type == pixel::CellTypes::Dirt, "feedback changed the published snapshot");

    pixel::World pushed(32, 32), free(32, 32);
    for (auto *world : {&pushed, &free})
        world->ConfigureWind(32, Uniform({120, 0}));
    pushed.PlaceDirt(8, 8);
    pushed.Tick(Step);
    free.Tick(Step);
    Check(pushed.GetCell(9, 9).Type == pixel::CellTypes::Dirt, "wind no longer pushes dirt");
    const auto affected = pushed.GetWind().Sample(9.5f, 9.5f);
    Check(affected.X < free.GetWind().Sample(9.5f, 9.5f).X && affected.Y > 0.1f,
          "wind-pushed dirt did not feed its actual motion back into the air");
    Check(pushed.GetCell(9, 9).Force == pixel::ForceDirection{0, 1}, "coupling transported or overwrote the base field");
}

void EditsAndCouplingScope() {
    pixel::World world(64, 64);
    world.FillForceDirection({0, 0});
    world.ConfigureWind(32, Uniform({20, 0}));
    world.PlaceDirt(20, 10);
    world.Tick(Step);
    Check(world.RemoveDirt(20, 10), "stationary feedback fixture unexpectedly moved");
    auto withoutDirt = world.GetWind();
    withoutDirt.Step(Step);
    world.Tick(Step);
    Check(Samples(world.GetWind(), 64, 32) == Samples(withoutDirt, 64, 32), "removed dirt left stale drag or motion behind");
    world.PlaceDirt(20, 10);
    withoutDirt = world.GetWind();
    withoutDirt.Step(Step);
    world.Tick(Step);
    Check(Samples(world.GetWind(), 64, 32) != Samples(withoutDirt, 64, 32), "new dirt did not enter the feedback mask");
    world.GenerateTerrain(32);
    Check(!world.GetWind().IsEnabled(), "terrain reset retained wind feedback");
    world.ConfigureWind(32, Uniform({20, 0}));
    withoutDirt = world.GetWind();
    withoutDirt.Step(Step);
    world.Tick(Step);
    Check(Samples(world.GetWind(), 64, 32) == Samples(withoutDirt, 64, 32), "dirt outside the wind region changed the wind");

    auto settings = Uniform({20, 0});
    settings.DirtDrag = 0;
    world.Fill({pixel::CellTypes::Dirt});
    world.ConfigureWind(64, settings);
    withoutDirt = world.GetWind();
    withoutDirt.Step(Step);
    world.Tick(Step);
    Check(Samples(world.GetWind(), 64, 64) == Samples(withoutDirt, 64, 64), "DirtDrag=0 did not disable feedback");
    world.Fill({{20}}); // Non-dirt material is outside this coupling contract.
    world.ConfigureWind(64, Uniform({20, 0}));
    withoutDirt = world.GetWind();
    withoutDirt.Step(Step);
    world.Tick(Step);
    Check(Samples(world.GetWind(), 64, 64) == Samples(withoutDirt, 64, 64), "non-dirt material unexpectedly entered the drag mask");
}

void FeedbackValidation() {
    pixel::World world(8, 8);
    world.PlaceDirt(3, 3);
    pixel::WindField wind;
    wind.Reset(8, 8, Uniform({20, 0}));
    const auto before = Samples(wind, 8, 8);
    auto reject = [&](auto action) {
        bool rejected = false;
        try {
            action();
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        Check(rejected && Samples(wind, 8, 8) == before, "invalid feedback input changed the field");
    };
    for (float value : {-1.f, 1001.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        auto settings = Uniform();
        settings.DirtDrag = value;
        reject([&] { wind.Reset(8, 8, settings); });
    }
    auto malformed = world.ReadState();
    malformed.Cells = malformed.Cells.first(1);
    reject([&] { wind.CoupleDirt(malformed, {}, Step); });
    for (auto moved : {pixel::DirtMotion{8, 0, {0, 0}}, pixel::DirtMotion{0, 0, {60, 0}}, pixel::DirtMotion{3, 3, {1001, 0}}})
        reject([&] { wind.CoupleDirt(world.ReadState(), std::span(&moved, 1), Step); });
    for (float invalidTime : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
        wind.CoupleDirt(world.ReadState(), {}, invalidTime);
    Check(Samples(wind, 8, 8) == before, "invalid time applied dirt feedback");
}
} // namespace

void RunPixelDirtWindTests() {
    CoverageAndDrag();
    StaticDirtDeflectsAir();
    NarrowStackInProductionWorld();
    ActualMovementFeedback();
    EditsAndCouplingScope();
    FeedbackValidation();
}
