#include "../Support/WorldAccess.h"
#include "Runtime/Gameplay/FormulaBridge/FormulaBridge.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
formula::process::ProgramArtifact Program(std::string_view source) {
    return std::get<formula::process::ProgramArtifact>(formula::process::Compile("gameplay.process", source));
}

void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

WorldPosition At(double x, double y) { return WorldPosition::FromAbsolute(x, y); }

void DefaultAndVersioning() {
    World world(64, 64);
    FormulaBridge runtime;
    Require(runtime.Cast(world, At(10.5, 10.5), 0), "default activation failed");
    Require(runtime.Projectiles().size() == 1 && runtime.Projectiles()[0].Damage == 10 && runtime.AppliedProgram().ResourceId == 2,
            "default must produce a process bolt");
    runtime.Step(world);
    Require(std::abs(runtime.Projectiles()[0].Position.RelativeTo({10, 10})[0] - 1.5) < 1e-9, "bolt did not move one cell per tick");
    runtime.Clear(world);
    runtime.ApplyProcess(Program("process 1;"));
    Require(runtime.Cast(world, At(10.5, 10.5), 0) && runtime.Stats().Roots == 0, "empty process must retire");
    runtime.ApplyProcess(Program("process 1; wait ticks(3); emit bolt(30, speed(0), ticks(10)); wait ticks(10);"));
    const auto oldVersion = runtime.Version();
    Require(runtime.Cast(world, At(10.5, 10.5), 0), "delayed cast failed");
    runtime.ApplyProcess(Program("process 1; emit bolt(99, speed(0), ticks(10)); wait ticks(10);"));
    Require(runtime.Cast(world, At(20.5, 10.5), 0), "new activation failed");
    runtime.Step(world); runtime.Step(world);
    Require(runtime.Projectiles().size() == 1, "delay advanced early");
    runtime.Step(world);
    bool old = false;
    for (const auto &bolt : runtime.Projectiles()) old |= bolt.Version == oldVersion && bolt.Damage == 30;
    Require(runtime.Projectiles().size() == 2 && old, "old instance lost its program version");
}

void SweptGeometry() {
    World world(64, 64);
    world.SetCell(15, 10, {CellTypeId::Stone});
    auto hit = FormulaWorldAdapter::Sweep(world, At(10.5, 10.5), At(20.5, 10.5));
    Require(hit.Kind == FormulaContactKind::Solid && hit.Cell == CellCoord{15, 10}, "fast ball tunneled through thin wall");
    Require(std::abs(hit.Fraction - .415) < 1e-9 && std::abs(hit.Position.RelativeTo({14, 10})[0] - .65) < 1e-9,
            "sweep did not return contact ball center");
    world.SetCell(15, 10, {});
    world.SetCell(10, 10, {CellTypeId::Stone});
    Require(FormulaWorldAdapter::Sweep(world, At(9.7, 9.7), At(9.7, 9.7)).Kind == FormulaContactKind::None,
            "expanded AABB produced false corner collision");
    Require(FormulaWorldAdapter::Sweep(world, At(9.8, 9.8), At(9.8, 9.8)).Kind == FormulaContactKind::Solid,
            "zero velocity rounded-corner overlap was missed");
    const double tangent = 10 - FormulaWorldAdapter::BoltRadius / std::sqrt(2.0);
    Require(FormulaWorldAdapter::Sweep(world, At(tangent, tangent), At(tangent, tangent)).Kind == FormulaContactKind::Solid,
            "closed-set tangent must count as contact");
    world.SetCell(10, 10, {});
}

void ClosedSetFaceTangencies() {
    struct SolidCase {
        CellCoord Cell;
        WorldPosition Position;
        FormulaVector AlongFace;
    };
    const SolidCase solids[] = {
        {{0, 10}, At(1.35, 10.5), {0, .1}},
        {{2, 10}, At(1.65, 10.5), {0, .1}},
        {{10, 0}, At(10.5, 1.35), {.1, 0}},
        {{10, 2}, At(10.5, 1.65), {.1, 0}},
    };
    World world(16, 16);
    for (const auto &test : solids) {
        world.SetCell(test.Cell.X, test.Cell.Y, {CellTypeId::Stone});
        for (const bool moving : {false, true}) {
            const auto to = moving ? FormulaWorldAdapter::Offset(test.Position, test.AlongFace) : test.Position;
            const auto contact = FormulaWorldAdapter::Sweep(world, test.Position, to);
            Require(contact.Kind == FormulaContactKind::Solid && contact.Cell == test.Cell && contact.Fraction == 0,
                    "face tangency must include both adjacent candidate cells for stationary and parallel motion");
        }
        world.SetCell(test.Cell.X, test.Cell.Y, {});
    }

    struct BoundaryCase {
        WorldPosition Position;
        FormulaVector AlongFace;
    };
    const BoundaryCase boundaries[] = {
        {At(.35, 8.5), {0, .1}},
        {At(15.65, 8.5), {0, .1}},
        {At(8.5, .35), {.1, 0}},
        {At(8.5, 15.65), {.1, 0}},
    };
    for (const auto &test : boundaries) {
        for (const bool moving : {false, true}) {
            const auto to = moving ? FormulaWorldAdapter::Offset(test.Position, test.AlongFace) : test.Position;
            const auto contact = FormulaWorldAdapter::Sweep(world, test.Position, to);
            Require(contact.Kind == FormulaContactKind::OutOfSimulation && contact.Fraction == 0,
                    "closed-set tangency with each simulation boundary must discard without entering unknown space");
        }
    }
}

void ClosedSetDigCenters() {
    World world(8, 8);
    // Each center is exactly one small radius from the tested cell center.
    // Nonbinary fractions expose both lower and upper candidate rounding.
    for (const auto position : {At(1.85, 1.5), At(1.15, 1.5), At(1.5, 1.85), At(1.5, 1.15)}) {
        world.SetCell(1, 1, {CellTypeId::Stone});
        Require(FormulaWorldAdapter::Dig(world, position, .35).Applied == 1,
                "dig candidate bounds omitted a cell center on the closed circle boundary");
    }
    for (const auto position : {At(1.850001, 1.5), At(1.149999, 1.5), At(1.5, 1.850001), At(1.5, 1.149999)}) {
        world.SetCell(1, 1, {CellTypeId::Stone});
        Require(FormulaWorldAdapter::Dig(world, position, .35).Applied == 0 && world.GetCell(1, 1).Type == CellTypeId::Stone,
                "dig conservatively included a candidate but failed to exclude its center outside the circle");
    }
}

void DomainExitAndDigRules() {
    World world(16, 16);
    world.SetCell(15, 8, {CellTypeId::Stone});
    FormulaBridge runtime;
    runtime.ApplyProcess(Program("process 1; emit bolt(10, speed(600), ticks(5)); await hit within ticks(10); if outcome() == hit { emit dig(cells(3)); }"));
    runtime.Cast(world, At(14.5, 4.5), 0);
    runtime.Step(world);
    Require(runtime.Projectiles().empty() && world.GetCell(15, 8).Type == CellTypeId::Stone,
            "domain exit must discard without payload");
    world.SetCell(4, 4, {CellTypeId::Stone});
    world.SetCell(5, 4, {CellTypeId::GoldOre});
    world.SetCell(4, 5, {CellTypeId::Water});
    const auto result = FormulaWorldAdapter::Dig(world, At(4.5, 4.5), 2);
    Require(result.Applied == 2 && world.GetCell(4, 5).Type == CellTypeId::Water, "dig did not use Mineable material rules");
    world.SetCell(6, 6, {CellTypeId::CopperOre});
    Require(FormulaWorldAdapter::Dig(world, At(6.01, 6.99), 0).Applied == 1, "zero-radius dig did not select containing cell");

    World sparse;
    pixel::testing::WorldAccess::Map(sparse).Insert(std::make_unique<WorldChunk>(CellCoord{0, 0}));
    runtime.Clear(world);
    runtime.Cast(sparse, At(10.5, 4.5), 0);
    auto unloaded = pixel::testing::WorldAccess::Map(sparse).Remove({0, 0});
    runtime.Step(sparse);
    Require(runtime.Projectiles().empty() && runtime.Stats().Roots == 1,
            "missing chunk must freeze without hit or forced residency");
    pixel::testing::WorldAccess::Map(sparse).Insert(std::move(unloaded));
    runtime.SynchronizeActivity(sparse);
    Require(runtime.Projectiles().size() == 1 && runtime.Projectiles()[0].Position.Cell().X == 10,
            "chunk return lost or advanced process projectile");
}

WindStrategySettings Coarse() {
    WindStrategySettings settings;
    settings.Kind = WindStrategyKind::Coarse;
    settings.Coarse.Columns = settings.Coarse.Rows = 32;
    settings.Coarse.Ambient = {};
    settings.Coarse.GustSpeed = settings.Coarse.Relaxation = settings.Coarse.Viscosity = 0;
    return settings;
}

void WindAdapterBoundaries() {
    World world(64, 64);
    Require(FormulaWorldAdapter::Wind(world, At(10, 10), 1, {60, 0}) == FormulaWindResult::Unavailable, "disabled wind was silently enabled");
    world.ConfigureWindStrategy(64, Coarse());
    Require(FormulaWorldAdapter::Wind(world, At(10, 10), 1.0 / 256, {60, 0}) == FormulaWindResult::Applied, "minimum resolved radius was rejected");
    Require(FormulaWorldAdapter::Wind(world, At(64 - 1e-10, 10), 1, {60, 0}) == FormulaWindResult::Applied, "domain-inside rounding reached outside");
    Require(FormulaWorldAdapter::Wind(world, At(64, 10), 1, {60, 0}) == FormulaWindResult::Unavailable, "outside wind was clamped into domain");
    auto tooLarge = Coarse(); tooLarge.Coarse.Columns = 33;
    world.ConfigureWindStrategy(64, tooLarge);
    Require(FormulaWorldAdapter::Wind(world, At(10, 10), 1, {60, 0}) == FormulaWindResult::GridUnsupported, "grid limit was ignored");
}

void LimitsAndRetirement() {
    World world(64, 64);
    FormulaBridge runtime;
    runtime.ApplyProcess(Program("process 1; wait ticks(600);"));
    for (int frame = 0; frame < 2; ++frame) {
        runtime.BeginFrame();
        for (int i = 0; i < 8; ++i) Require(runtime.Cast(world, At(30, 30), 0), "legal instance rejected");
        Require(!runtime.Cast(world, At(30, 30), 0), "frame activation limit ignored");
    }
    runtime.BeginFrame();
    Require(runtime.Stats().Roots == 16 && !runtime.Cast(world, At(30, 30), 0), "instance limit ignored");
    runtime.Clear(world);
    runtime.BeginFrame();
    runtime.ApplyProcess(Program("process 1;"));
    for (int i = 0; i < 8; ++i) Require(runtime.Cast(world, At(30, 30), 0), "instant process rejected");
    Require(!runtime.Cast(world, At(30, 30), 0), "instant retirement bypassed activation limit");
    const auto epoch = runtime.WorldEpoch();
    runtime.OnWorldReplaced(world);
    Require(runtime.WorldEpoch() == epoch + 1 && runtime.Stats().Roots == 0, "world replacement retained work");
}

void DistantCoordinates() {
    const auto directory = std::filesystem::temp_directory_path() / ("pixel-formula-far-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        for (const int64_t far : {(int64_t{1} << 53) + 17, -(int64_t{1} << 53) - 17}) {
            World world;
            WorldStreamSettings settings;
            settings.Directory = directory / std::to_string(far);
            settings.WindowEdge = storage::ChunkEdge;
            settings.Generation.Kind = WorldGeneratorKind::Flat;
            settings.Generation.SurfaceY = far + 100;
            settings.SurfaceY = far + 100;
            const int64_t baseX = FloorDivide(far, storage::ChunkEdge) * storage::ChunkEdge;
            const int64_t baseY = FloorDivide(far, storage::ChunkEdge) * storage::ChunkEdge;
            world.EnableStreaming(settings, {baseX, baseY, storage::ChunkEdge, storage::ChunkEdge});
            world.FillRegion(world.GetSimulationBounds(), {});
            WorldPosition anchor{ChunkOf({baseX, baseY}), {10.25, 10.5}};
            world.SetCell(baseX + 15, baseY + 10, {CellTypeId::Stone});
            FormulaBridge runtime;
            runtime.Cast(world, anchor, 0);
            runtime.Step(world);
            Require(runtime.Projectiles()[0].Position.Cell() == CellCoord{baseX + 11, baseY + 10}
                    && std::abs(runtime.Projectiles()[0].Position.RelativeTo({baseX + 11, baseY + 10})[0] - .25) < 1e-10,
                    "far coordinate bolt lost fractional movement");
            for (int i = 0; i < 4; ++i)
                runtime.Step(world);
            Require(runtime.Projectiles().empty(), "far coordinate wall collision lost precision");
            world.ConfigureWindDomain(baseY + storage::ChunkEdge, Coarse());
            Require(FormulaWorldAdapter::Wind(world, anchor, 2, {60, 0}) == FormulaWindResult::Applied, "far coordinate wind did not use integer-relative conversion");
        }
    } catch (...) {
        std::filesystem::remove_all(directory);
        throw;
    }
    std::filesystem::remove_all(directory);
}
} // namespace

int main() {
    try {
        DefaultAndVersioning();
        SweptGeometry();
        ClosedSetFaceTangencies();
        ClosedSetDigCenters();
        DomainExitAndDigRules();
        WindAdapterBoundaries();
        LimitsAndRetirement();
        DistantCoordinates();
        std::cout << "Pixel formula gameplay contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
