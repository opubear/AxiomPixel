#include "World/Environment/Wind/LocalWindGeometry.h"
#include "World/Environment/Wind/ProceduralWindField.h"
#include "World/Environment/Wind/WindSimulation.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void Near(WindVelocity a, WindVelocity b, float tolerance, const char *message) {
    Check(std::hypot(a.X - b.X, a.Y - b.Y) <= tolerance, message);
}
WindStrategySettings Uniform(WindStrategyKind kind, WindVelocity ambient = {8, 0}) {
    WindStrategySettings settings;
    settings.Kind = kind;
    settings.Coarse.Ambient = settings.Reference.Ambient = settings.Adaptive.Flow.Ambient = settings.Lbm.Ambient = ambient;
    settings.Coarse.GustSpeed = settings.Reference.GustSpeed = settings.Adaptive.Flow.GustSpeed = settings.Lbm.GustSpeed = 0;
    settings.Coarse.Relaxation = settings.Reference.Relaxation = settings.Adaptive.Flow.Relaxation = settings.Lbm.Relaxation = 0;
    settings.Coarse.Viscosity = settings.Reference.Viscosity = settings.Adaptive.Flow.Viscosity = settings.Lbm.Viscosity = 0;
    settings.Coarse.Columns = 12;
    settings.Coarse.Rows = 8;
    return settings;
}
void UniformOpenDomains() {
    WorldMap map(128, 96);
    constexpr std::array kinds{WindStrategyKind::Coarse, WindStrategyKind::Reference, WindStrategyKind::Procedural,
                               WindStrategyKind::Hybrid, WindStrategyKind::Adaptive, WindStrategyKind::Lbm, WindStrategyKind::Empty};
    for (auto kind : kinds)
        for (WindVelocity velocity : {WindVelocity{8, 0}, {-8, 0}, {0, 8}, {0, -8}, {5, -3}, {}}) {
            WindSimulation simulation;
            auto first = map.View({16, 16, 48, 32});
            simulation.Configure(first, first.Height, Uniform(kind, velocity));
            simulation.AdvanceBeforeMaterials(first, 1.f / 60);
            const auto expected = kind == WindStrategyKind::Empty ? WindVelocity{} : velocity;
            Near(simulation.SampleWorld(40.5, 32.5), expected, .001f, "uniform open domain changed flow");
            auto next = map.View({24, 20, 48, 32});
            simulation.MoveDomain(next, next.Height);
            Near(simulation.SampleWorld(40.5, 32.5), expected, .001f, "domain migration changed overlap flow");
            simulation.AdvanceBeforeMaterials(next, 1.f / 60);
            Near(simulation.SampleWorld(40.5, 32.5), expected, .001f, "migrated open domain lost uniform flow");
            Check(simulation.GetDomain().Origin == next.Origin, "domain did not track world origin");
            Near(simulation.SampleForMaterial(CellCoord{40, 32}), expected, .001f, "material sampling did not convert world coordinates");
            Check(simulation.SampleWorld(23.5, 32) == WindVelocity{}, "sample outside domain is not calm");
            if (const auto *lbm = simulation.GetLbm()) {
                Check(std::abs(lbm->GetReport().Mass - 48 * 32) < .001, "uniform LBM open domain changed mass");
                Check(lbm->GetReport().Stabilizations == 0, "uniform LBM required stabilization");
            }
        }
}
void InactiveDomainRecovery() {
    WorldMap map(96, 64);
    WindSimulation simulation;
    auto empty = map.View({16, 16, 48, 0});
    simulation.Configure(empty, 0, Uniform(WindStrategyKind::Reference));
    Check(simulation.GetKind() == WindStrategyKind::Reference && !simulation.GetReference().IsEnabled(), "empty domain discarded configuration");
    auto full = map.View({16, 16, 48, 32});
    simulation.AdvanceBeforeMaterials(full, 1.f / 60);
    Near(simulation.SampleWorld(30.5, 30.5), {8, 0}, .001f, "empty domain did not recover");
    simulation.MoveDomain(empty, 0);
    Check(!simulation.GetReference().IsEnabled(), "inactive domain retained solver storage");
    simulation.AdvanceBeforeMaterials(full, 1.f / 60);
    Near(simulation.SampleWorld(30.5, 30.5), {8, 0}, .001f, "second recovery did not restore configuration");
}
void ProceduralPhaseAndClippedImpulses() {
    WindSettings settings;
    settings.GustSpeed = 3;
    settings.Relaxation = 0;
    ProceduralWindField wind(48, 32, settings);
    wind.SetOrigin({-1000000000000, -1000000000000});
    wind.Step(.1f);
    const auto before = wind.Sample(24.5f, 16.5f);
    wind.MoveDomain(48, 32, {-999999999992, -999999999996});
    Near(wind.Sample(16.5f, 12.5f), before, .00001f, "procedural phase changed during domain migration");
    const auto far = wind.Sample(47.5f, 10.5f);
    wind.AddImpulse(.5f, 10.5f, 3, {10, 0});
    Near(wind.Sample(47.5f, 10.5f), far, 0, "open impulse wrapped across domain edge");
}
void LocalGeometryDoesNotWrap() {
    WorldMap map(48, 32);
    map.SetMaterial({0, 16}, CellTypes::Dirt);
    LocalWindGeometry geometry(48, 32, {});
    geometry.Update(map.Read());
    Near(geometry.Constrain(47.5f, 16.5f, {8, 0}), {8, 0}, 0, "local obstacle wrapped across open domain edge");
}
void LbmRemainderAndMass() {
    WorldMap map(96, 64);
    auto first = map.View({16, 16, 48, 32});
    auto settings = Uniform(WindStrategyKind::Lbm).Lbm;
    LbmWindField wind(first, first.Height, settings);
    wind.Step(.001f);
    Check(wind.GetReport().Substeps == 0, "LBM advanced before a full lattice step");
    auto shifted = map.View({24, 20, 48, 32});
    wind.MoveDomain(shifted, shifted.Height, 8, 4);
    wind.Step(.001f);
    Check(wind.GetReport().Substeps == 1, "LBM migration discarded substep remainder");
    settings.Boundary = WindBoundaryMode::Periodic;
    LbmWindField periodic(first, first.Height, settings);
    periodic.AddImpulse(10, 10, 5, {4, 1});
    const double mass = periodic.GetReport().Mass;
    for (int step = 0; step < 30; ++step)
        periodic.Step(1.f / 60);
    Check(std::abs(periodic.GetReport().Mass - mass) < 1e-7, "explicit periodic LBM mode lost mass");
}
void PerturbedMigrationAndOpenLbmStability() {
    WorldMap map(128, 96);
    const auto first = map.View({16, 16, 48, 32}), shifted = map.View({20, 20, 48, 32});
    for (auto kind : {WindStrategyKind::Coarse, WindStrategyKind::Reference, WindStrategyKind::Procedural,
                      WindStrategyKind::Hybrid, WindStrategyKind::Adaptive, WindStrategyKind::Lbm}) {
        auto settings = Uniform(kind);
        settings.Coarse.Viscosity = settings.Reference.Viscosity = settings.Adaptive.Flow.Viscosity = settings.Lbm.Viscosity = 8;
        WindSimulation simulation;
        simulation.Configure(first, first.Height, settings);
        simulation.AddImpulse(24, 16, 10, {6, 3});
        simulation.AdvanceBeforeMaterials(first, 1.f / 60);
        std::vector<WindVelocity> before;
        for (int y = 24; y < 40; ++y)
            for (int x = 28; x < 52; ++x)
                before.push_back(simulation.SampleWorld(x + .5, y + .5));
        simulation.MoveDomain(shifted, shifted.Height);
        double squareError = 0;
        size_t i = 0;
        for (int y = 24; y < 40; ++y)
            for (int x = 28; x < 52; ++x) {
                const auto value = simulation.SampleWorld(x + .5, y + .5), old = before[i++];
                squareError += std::pow(value.X - old.X, 2) + std::pow(value.Y - old.Y, 2);
            }
        const double tolerance = kind == WindStrategyKind::Lbm || kind == WindStrategyKind::Procedural ? 1e-6 : 1;
        Check(std::sqrt(squareError / before.size()) < tolerance, "domain migration discarded a nonuniform overlap field");
        if (kind == WindStrategyKind::Lbm) {
            for (int step = 0; step < 120; ++step)
                simulation.AdvanceBeforeMaterials(shifted, 1.f / 60);
            const auto &report = simulation.GetLbm()->GetReport();
            Check(report.MinDensity > .98 && report.MaxDensity < 1.02 && report.Stabilizations == 0,
                  "low-Mach open LBM perturbation destabilized its boundary");
            Check(std::abs(report.Mass - 48 * 32) < 2, "open LBM perturbation left an unbounded mass drift");
        }
    }
}
void PublicationRevisionAndProceduralRecovery() {
    WorldMap map(96, 64);
    const auto full = map.View({16, 16, 48, 32});
    WindSimulation simulation;
    auto settings = Uniform(WindStrategyKind::Empty);
    simulation.Configure(full, full.Height, settings);
    const auto revision = simulation.GetRevision();
    simulation.AdvanceBeforeMaterials(full, 1.f / 60);
    simulation.UpdateAfterMaterials(full, {}, 1.f / 60);
    Check(simulation.GetRevision() == revision, "empty solver invalidated unchanged publication data");
    settings = Uniform(WindStrategyKind::Procedural);
    settings.Coarse.GustSpeed = 3;
    simulation.Configure(full, full.Height, settings);
    simulation.SetSimulationTime(50);
    const auto expected = simulation.SampleWorld(32.5, 30.5);
    auto empty = full;
    empty.Height = 0;
    simulation.MoveDomain(empty, 0);
    auto clipped = full;
    clipped.Height = 24;
    simulation.MoveDomain(clipped, clipped.Height);
    Near(simulation.SampleWorld(32.5, 30.5), expected, .00001f, "procedural phase changed after clipped domain recovery");
}
void TinyOpenDomains() {
    for (auto extent : {std::pair{1u, 1u}, {1u, 16u}, {16u, 1u}}) {
        WorldMap map(extent.first, extent.second);
        for (auto kind : {WindStrategyKind::Coarse, WindStrategyKind::Reference, WindStrategyKind::Procedural,
                          WindStrategyKind::Hybrid, WindStrategyKind::Adaptive, WindStrategyKind::Lbm}) {
            WindSimulation simulation;
            simulation.Configure(map.Read(), extent.second, Uniform(kind));
            simulation.AdvanceBeforeMaterials(map.Read(), 1.f / 60);
            Near(simulation.Sample(.5f, .5f), {8, 0}, .001f, "tiny open domain lost uniform flow");
        }
    }
}
void DistantCoordinatesPreserveAdjacentCellsAndMigration() {
    for (const int64_t origin : {int64_t{1} << 55, -(int64_t{1} << 55)}) {
        WorldMap map;
        const auto firstChunk = ChunkOf({origin, origin});
        const auto lastChunk = ChunkOf({origin + 65 + 128 - 1, origin + 96 - 1});
        for (int64_t y = firstChunk.Y; y <= lastChunk.Y; ++y)
            for (int64_t x = firstChunk.X; x <= lastChunk.X; ++x)
                map.Insert(std::make_unique<WorldChunk>(CellCoord{x, y}));
        map.SetMaterial({origin + 10, origin + 16}, CellTypes::Dirt);
        const auto first = map.View({origin, origin, 128, 96});
        for (const auto kind : {WindStrategyKind::Procedural, WindStrategyKind::Lbm}) {
            WindSimulation simulation;
            auto settings = Uniform(kind);
            simulation.Configure(first, first.Height, settings);
            for (int64_t x = 8; x <= 12; ++x) {
                const CellCoord cell{origin + x, origin + 16};
                Near(simulation.SampleWorld(cell), simulation.Sample(float(x) + .5f, 16.5f), 0,
                     "distant world sampling rounded adjacent integer cells");
                Near(simulation.SampleForMaterial(cell), simulation.SampleForMaterial(uint32_t(x), 16), 0,
                     "distant material sampling rounded adjacent integer cells");
            }
            Check(simulation.SampleWorld(CellCoord{origin + 10, origin + 16}) == WindVelocity{},
                  "distant solid cell returned an adjacent air velocity");
            Check(simulation.SampleWorld(CellCoord{origin + 11, origin + 16}).X > 0,
                  "distant air cell aliased the adjacent solid");
            simulation.AddImpulse(90.5f, 40.5f, 6, {12, 3});
            const CellCoord target{origin + 90, origin + 40};
            const auto expected = simulation.SampleWorld(target);
            Check(expected.X != simulation.SampleWorld(CellCoord{origin + 91, origin + 40}).X,
                  "distant sample fixture lacks adjacent velocity variation");
            for (const int64_t shift : {int64_t{1}, int64_t{65}}) {
                const auto moved = map.View({origin + shift, origin, 128, 96});
                simulation.MoveDomain(moved, moved.Height);
                Near(simulation.SampleWorld(target), expected, .00001f,
                     "distant domain migration rounded a one/64-cell origin shift");
                Near(simulation.SampleForMaterial(target), simulation.SampleForMaterial(uint32_t(90 - shift), 40), 0,
                     "distant migrated material sampling lost its integer offset");
            }
        }
    }
}
void ReferenceProjectionWithObstacles() {
    WorldMap map(64, 48);
    for (int y = 12; y < 36; ++y)
        map.SetMaterial({32, y}, CellTypes::Dirt);
    auto view = map.Read();
    auto settings = Uniform(WindStrategyKind::Reference).Reference;
    PixelWindField wind;
    wind.Reset(view.Width, view.Height, settings);
    wind.SetObstacles(view);
    wind.Step(1.f / 60);
    Check(wind.GetReport().Converged && wind.GetReport().RmsDivergence < 1.1e-6, "open reference obstacle projection did not converge");
    for (int y = 12; y < 36; ++y)
        Check(std::abs(wind.FaceX(32, uint32_t(y))) < 1e-10, "reference obstacle admitted normal flow");
    double netFlux = 0;
    for (uint32_t y = 0; y < view.Height; ++y)
        netFlux += wind.FaceX(view.Width, y) - wind.FaceX(0, y);
    for (uint32_t x = 0; x < view.Width; ++x)
        netFlux += wind.FaceY(x, view.Height) - wind.FaceY(x, 0);
    Check(std::abs(netFlux) < .0001, "open reference projection did not balance boundary flux");
}
} // namespace
int main() {
    try {
        UniformOpenDomains();
        InactiveDomainRecovery();
        ProceduralPhaseAndClippedImpulses();
        LocalGeometryDoesNotWrap();
        LbmRemainderAndMass();
        ReferenceProjectionWithObstacles();
        PerturbedMigrationAndOpenLbmStability();
        PublicationRevisionAndProceduralRecovery();
        TinyOpenDomains();
        DistantCoordinatesPreserveAdjacentCellsAndMigration();
        std::cout << "Wind domain tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
