#include "../Support/WorldAccess.h"
#include "Gameplay/FormulaBridge/FormulaBridge.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace pixel;
using formula::Formula;

void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

bool HasDiagnostic(const FormulaBridge &runtime, const std::string &text) {
    for (const auto &entry : runtime.Diagnostics())
        if (entry.Message.find(text) != std::string::npos)
            return true;
    return false;
}

WorldPosition At(double x, double y) { return WorldPosition::FromAbsolute(x, y); }

void DefaultAndVersioning() {
    World world(64, 64);
    FormulaBridge runtime;
    Require(runtime.Cast(world, At(10.5, 10.5), 0), "default activation failed");
    Require(runtime.Projectiles().size() == 1 && runtime.Projectiles()[0].Damage == 10, "default must only produce its final bolt");
    runtime.Step(world);
    Require(std::abs(runtime.Projectiles()[0].Position.RelativeTo({10, 10})[0] - 1.5) < 1e-9, "bolt did not move one cell per tick");
    runtime.Clear(world);

    Require(runtime.Apply(Formula{}), "empty apply failed");
    Require(runtime.Cast(world, At(10.5, 10.5), 0) && runtime.Projectiles().empty() && runtime.Stats().Roots == 0, "empty must not create a placeholder");
    Require(runtime.Apply(Formula::Bolt(30, 0).After(3)), "delayed apply failed");
    const auto oldVersion = runtime.Version();
    Require(runtime.Cast(world, At(10.5, 10.5), 0), "delayed cast failed");
    Require(runtime.Projectiles().empty() && runtime.Stats().Roots == 1 && runtime.Stats().Pending == 1, "delayed root retired prematurely");
    Require(runtime.Apply(Formula::Bolt(99, 0)), "new apply failed");
    Require(runtime.Cast(world, At(20.5, 10.5), 0), "new version activation failed");
    const auto goodVersion = runtime.Version();
    Require(!runtime.Apply(Formula::Bolt().Spread(17, 10)) && runtime.Version() == goodVersion, "invalid apply replaced active description");
    runtime.Step(world);
    runtime.Step(world);
    Require(runtime.Projectiles().size() == 1, "delay advanced early");
    runtime.Step(world);
    Require(runtime.Projectiles().size() == 2 && runtime.Projectiles()[0].Version == oldVersion && runtime.Projectiles()[0].Damage == 30,
            "running root did not preserve its description version");
}

void SweptGeometryAndExpiry() {
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
    world.SetCell(15, 10, {CellTypeId::Stone});

    FormulaBridge runtime;
    Require(runtime.Apply(Formula::Bolt(10, 600, 1).OnHit(Formula::Dig(3))), "expiry formula rejected");
    runtime.Cast(world, At(10.5, 10.5), 0);
    runtime.Step(world);
    Require(runtime.Projectiles().empty() && !HasDiagnostic(runtime, "hit id=") && world.GetCell(15, 10).Type == CellTypeId::Stone,
            "excluded expiration segment produced a false Hit");

    runtime.Apply(Formula::Bolt(10, 600, 5).OnHit(Formula::Dig(3)));
    runtime.Cast(world, At(10.5, 10.5), 0);
    runtime.Step(world);
    Require(runtime.Projectiles().empty() && world.GetCell(15, 10).Type == CellTypeId::Stone, "hit payload ran in the collision tick");
    runtime.Step(world);
    Require(world.GetCell(15, 10).Type == CellTypeId::Air && HasDiagnostic(runtime, "Dig applied=1"), "hit payload did not run on the next tick");
}

void ClosedSetFaceTangencies() {
    struct SolidCase {
        CellCoord Cell;
        WorldPosition Position;
        formula::Vec2 AlongFace;
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
        formula::Vec2 AlongFace;
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

void ImmediateHitsAndMapConsistency() {
    World world(64, 64);
    world.SetCell(10, 10, {CellTypeId::Stone});
    FormulaBridge runtime;
    runtime.Apply(Formula::Bolt(10, 0).OnHit(Formula::Bolt(7, 0)));
    runtime.Cast(world, At(10.5, 10.5), 0);
    Require(runtime.Projectiles().empty(), "tick-zero overlap did not consume carrier");
    world.SetCell(10, 10, {});
    runtime.Step(world);
    Require(runtime.Projectiles().size() == 1 && runtime.Projectiles()[0].Damage == 7, "overlap payload did not wait exactly one tick");
    runtime.Clear(world);

    world.SetCell(15, 10, {CellTypeId::Stone});
    runtime.Apply(Formula::Sequence({Formula::Dig(3).After(1), Formula::Bolt(10, 600)}));
    runtime.Cast(world, At(10.5, 10.5), 0);
    runtime.Apply(Formula::Bolt(10, 600).OnHit(Formula::Dig(1)));
    runtime.Cast(world, At(10.5, 10.5), 0);
    runtime.Step(world);
    Require(runtime.Projectiles().empty(), "world effects were applied before all same-tick collision queries");
    Require(HasDiagnostic(runtime, "hit id="), "same-tick shared wall was not hit");
}

void DomainExitAndDigRules() {
    World world(16, 16);
    world.SetCell(15, 8, {CellTypeId::Stone});
    FormulaBridge runtime;
    runtime.Apply(Formula::Bolt(10, 600).OnHit(Formula::Dig(3)));
    runtime.Cast(world, At(14.5, 4.5), 0);
    runtime.Step(world);
    Require(runtime.Projectiles().empty() && HasDiagnostic(runtime, "OutOfSimulation") && !HasDiagnostic(runtime, "hit id="),
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
    pixel::testing::WorldAccess::Map(sparse).Remove({0, 0});
    runtime.Step(sparse);
    Require(runtime.Projectiles().empty() && HasDiagnostic(runtime, "NotReady") && !HasDiagnostic(runtime, "hit id="),
            "missing chunk must retire without hit or forced residency");
}

WindStrategySettings Coarse() {
    WindStrategySettings settings;
    settings.Kind = WindStrategyKind::Coarse;
    settings.Coarse.Columns = settings.Coarse.Rows = 32;
    settings.Coarse.Ambient = {};
    settings.Coarse.GustSpeed = settings.Coarse.Relaxation = settings.Coarse.Viscosity = 0;
    return settings;
}

void WindBoundariesAndDelayedConfiguration() {
    World world(64, 64);
    Require(FormulaWorldAdapter::Wind(world, At(10, 10), 1, {60, 0}) == FormulaWindResult::Unavailable, "disabled wind was silently enabled");
    for (const auto radius : {0.0, 1e-300, 1.0 / 256}) {
        FormulaBridge runtime;
        runtime.Apply(Formula::Sequence({Formula::Wind(radius, 120), Formula::Bolt(10, 0).After(2)}));
        Require(runtime.Cast(world, At(10.5, 10.5), 0), "legal degenerate wind aborted root");
        runtime.Step(world);
        runtime.Step(world);
        Require(runtime.Projectiles().size() == 1, "skipped wind canceled later bolt");
    }
    world.ConfigureWindStrategy(64, Coarse());
    Require(FormulaWorldAdapter::Wind(world, At(10, 10), 1.0 / 256, {60, 0}) == FormulaWindResult::Applied, "minimum resolved radius was rejected");
    Require(FormulaWorldAdapter::Wind(world, At(64 - 1e-10, 10), 1, {60, 0}) == FormulaWindResult::Applied, "domain-inside float rounding reached outside boundary");
    Require(FormulaWorldAdapter::Wind(world, At(64, 10), 1, {60, 0}) == FormulaWindResult::Unavailable, "outside wind point was clamped into domain");
    auto tooLarge = Coarse();
    tooLarge.Coarse.Columns = 33;
    world.ConfigureWindStrategy(64, tooLarge);
    Require(FormulaWorldAdapter::Wind(world, At(10, 10), 1, {60, 0}) == FormulaWindResult::GridUnsupported, "grid larger than 1024 cells was accepted");

    FormulaBridge runtime;
    runtime.Apply(Formula::Sequence({Formula::Wind(1, 60).After(2), Formula::Bolt(10, 0).After(3)}));
    runtime.Cast(world, At(10.5, 10.5), 0);
    auto other = Coarse();
    other.Kind = WindStrategyKind::Procedural;
    world.ConfigureWindStrategy(64, other);
    runtime.Step(world);
    runtime.Step(world);
    Require(HasDiagnostic(runtime, "WindBackendUnsupported"), "delayed wind did not recheck changed backend");
    runtime.Step(world);
    Require(runtime.Projectiles().size() == 1, "unsupported delayed wind canceled normal descendant");
}

void LimitsAndRetirement() {
    World world(64, 64);
    FormulaBridge runtime;
    runtime.Apply(Formula::Bolt(10, 0).Spread(16, 10).Repeated(4, 1));
    for (int i = 0; i < 4; ++i)
        Require(runtime.Cast(world, At(30, 30), 0), "legal reserved root rejected");
    Require(runtime.Stats().ReservedEffects == 256 && !runtime.Cast(world, At(30, 30), 0), "session total effects did not pre-reject");
    runtime.Clear(world);
    runtime.Apply(Formula::Bolt(10, 0).After(600));
    for (int frame = 0; frame < 2; ++frame) {
        runtime.BeginFrame();
        for (int i = 0; i < 8; ++i)
            Require(runtime.Cast(world, At(30, 30), 0), "legal delayed root rejected");
    }
    runtime.BeginFrame();
    Require(runtime.Stats().Roots == 16 && !runtime.Cast(world, At(30, 30), 0), "root limit ignored delayed instances without balls");
    runtime.Clear(world);
    runtime.BeginFrame();
    runtime.Apply(Formula{});
    for (int i = 0; i < 8; ++i)
        Require(runtime.Cast(world, At(30, 30), 0), "frame accepted fewer than 8 activations");
    Require(!runtime.Cast(world, At(30, 30), 0), "frame activation limit was bypassed by instant retirement");

    runtime.BeginFrame();
    runtime.Apply(Formula::Sequence({Formula::Wind(1, 60).Repeated(16, 0), Formula::Wind(1, 60).Repeated(16, 0),
                                  Formula::Wind(1, 60).Repeated(16, 0), Formula::Wind(1, 60).Repeated(16, 0)}));
    world.ConfigureWindStrategy(64, Coarse());
    const auto started = std::chrono::steady_clock::now();
    Require(runtime.Cast(world, At(30, 30), 0), "maximum legal wind batch rejected");
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    Require(runtime.Stats().WindCallsThisFrame == 64 && runtime.Stats().RetiredWind == 64, "wind calls or retired reservations incorrect");
    const auto rejectStarted = std::chrono::steady_clock::now();
    for (int i = 0; i < 16; ++i)
        Require(!runtime.Cast(world, At(30, 30), 0), "instant wind retirement restored same-frame quota");
    const auto rejectElapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rejectStarted).count();
    Require(runtime.Stats().WindCallsThisFrame == 64, "rejected activations called wind backend");
    runtime.Clear(world);
    runtime.OnWorldReplaced(world);
    Require(!runtime.Cast(world, At(30, 30), 0), "clear/reset restored same-frame wind quota");
    runtime.BeginFrame();
    Require(runtime.Cast(world, At(30, 30), 0), "BeginFrame failed to release retired wind quota");
    std::cout << "64 Coarse impulses over 1024 cells: " << elapsed << " ms; backend calls=64; 16 same-frame rejections: " << rejectElapsed << " ms\n";

    runtime.Clear(world);
    runtime.BeginFrame();
    runtime.Apply(Formula::Wind(1, 60).Repeated(16, 2));
    Require(runtime.Cast(world, At(30, 30), 0) && runtime.Stats().ReservedWind == 16, "delayed wind root reservation missing");
    runtime.BeginFrame();
    Require(runtime.Stats().ReservedWind == 16, "BeginFrame refunded live root wind reservation");
    const auto epoch = runtime.WorldEpoch();
    runtime.OnWorldReplaced(world);
    Require(runtime.WorldEpoch() == epoch + 1 && runtime.Stats().Roots == 0 && runtime.Stats().RetiredWind == 16,
            "world replacement did not clear queues and preserve retired wind quota");
    Require(runtime.Diagnostics().empty(), "clear retained old runtime diagnostics");

    runtime.BeginFrame();
    runtime.Apply(Formula::Dig(0).Repeated(16, 0).Spread(4, 10));
    for (int i = 0; i < 8; ++i)
        runtime.Cast(world, At(30, 30), 0);
    Require(runtime.Diagnostics().size() == 256 && runtime.Stats().DiagnosticBytes > 0 && runtime.Stats().SnapshotBytes > 0,
            "diagnostic ring is unbounded or tracked snapshot capacity is missing");
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
            Require(runtime.Projectiles().empty() && HasDiagnostic(runtime, "hit id="), "far coordinate wall collision lost precision");
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
        SweptGeometryAndExpiry();
        ClosedSetFaceTangencies();
        ClosedSetDigCenters();
        ImmediateHitsAndMapConsistency();
        DomainExitAndDigRules();
        WindBoundariesAndDelayedConfiguration();
        LimitsAndRetirement();
        DistantCoordinates();
        std::cout << "Pixel formula gameplay contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
