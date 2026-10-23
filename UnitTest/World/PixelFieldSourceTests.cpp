#include "../Support/WorldAccess.h"
#include "World/Environment/FieldResponse.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace pixel;
void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

FieldSourceSpec Source(double x = 32.5, double y = 32.5, uint64_t lifetime = 10) {
    return {WorldPosition::FromAbsolute(x, y), 8, {3600, -3600}, lifetime};
}

template<class F> void Invalid(F operation, const char *message) {
    bool rejected = false;
    try { operation(); } catch (const std::invalid_argument &) { rejected = true; }
    Check(rejected, message);
}

void OwnershipUpdateAndStableComposition() {
    World world(128, 128);
    world.FillBaseForce({5, 3600});
    const auto revision = world.GetEnvironment().GetFieldSources().Revision();
    const auto first = world.InstallFieldSource(10, Source());
    Check(first && first->Id && world.GetEnvironment().GetFieldSources().Size() == 1,
          "field source installation failed");
    Check(world.GetForce(32, 32) == ForceVector{3605, 0}
        && world.GetForce(41, 32) == ForceVector{5, 3600}
        && world.GetForce(39, 39) == ForceVector{5, 3600}, "source disk or acceleration units changed");
    Check(world.GetBaseForceField().Get(32, 32) == ForceVector{5, 3600}
        && world.GetEnvironment().GetFieldSources().Revision() > revision, "source changed permanent base or missed revision");
    Check(!world.UpdateFieldSource(11, *first, Source(80.5, 32.5))
        && !world.StopFieldSource(11, *first) && !world.StopFieldSource(10, {}), "foreign or zero handle was accepted");
    Check(world.UpdateFieldSource(10, *first, Source(80.5, 32.5, 3)), "owned update failed");
    Check(world.GetForce(32, 32) == ForceVector{5, 3600}
        && world.GetForce(80, 32) == ForceVector{3605, 0}, "update retained old contribution");
    const auto updatedRevision = world.GetEnvironment().GetFieldSources().Revision();
    Check(!world.UpdateFieldSource(10, *first, Source(2, 2))
        && world.GetEnvironment().GetFieldSources().Sources()[0].Remaining == 3
        && world.GetEnvironment().GetFieldSources().Revision() == updatedRevision, "out-of-domain update was not atomic");

    auto a = Source(80.5, 32.5);
    a.Acceleration = {36000, 0};
    const auto second = world.InstallFieldSource(11, a);
    a.Acceleration = {.001f, 0};
    const auto third = world.InstallFieldSource(12, a);
    a.Acceleration = {-36000, 0};
    const auto fourth = world.InstallFieldSource(13, a);
    Check(second && third && fourth, "composition fixtures did not install");
    float expected = 3600;
    for (const float value : {36000.f, .001f, -36000.f}) expected += value;
    Check(world.GetForce(80, 32).X == 5 + expected, "composition changed stable installation order");
    Check(world.StopFieldSource(12, *third), "middle source removal failed");
    const auto values = world.GetEnvironment().GetFieldSources().Sources();
    Check(values.size() == 3 && values[0].Handle == *first && values[1].Handle == *second
        && values[2].Handle == *fourth, "removal reordered source identity");
    world.StopFieldSources(10);
    Check(world.GetEnvironment().GetFieldSources().Size() == 2 && world.GetForce(80, 32) == ForceVector{5, 3600},
          "owner cleanup removed another owner or retained acceleration");
    world.StopFieldSources(11);
    world.StopFieldSources(13);
    const auto stoppedRevision = world.GetEnvironment().GetFieldSources().Revision();
    world.StopFieldSources(13);
    Check(!world.StopFieldSource(10, *first) && world.GetEnvironment().GetFieldSources().Revision() == stoppedRevision,
          "repeated cleanup changed state");
}

void LifetimesWakeAndMaterialMotion() {
    World world(64, 64);
    world.FillBaseForce({0, 0});
    world.SetCell(20, 20, {CellTypeId::Dirt, {0, 0}});
    Check(world.StepEnvironment(0) && world.StepEnvironment(FieldResponse::StepSeconds), "initial sleep steps failed");
    Check(world.GetEnvironment().LastActiveRegions() == 0, "fixture did not settle before installation");
    auto spec = Source(24.5, 20.5, 3);
    spec.Acceleration = {3600, 0};
    const auto handle = world.InstallFieldSource(1, spec);
    Check(handle.has_value(), "motion source installation failed");
    for (uint64_t step = 0; step < 3; ++step) {
        Check(world.StepEnvironment(double(step + 2) * FieldResponse::StepSeconds), "source material step failed");
        Check(world.GetCell(int64_t(21 + step), 20).Type == CellTypeId::Dirt, "source did not move actual dirt one cell per step");
        if (step < 2)
            Check(world.GetEnvironment().GetFieldSources().Sources()[0].Remaining == 2 - step, "source lifetime did not age once");
    }
    Check(world.GetEnvironment().GetFieldSources().Size() == 0 && !world.StopFieldSource(1, *handle), "expired source remained live");
    Check(world.StepEnvironment(5 * FieldResponse::StepSeconds) && world.GetCell(23, 20).Type == CellTypeId::Dirt,
          "expiry retained a force or rolled back earlier movement");
    Check(world.GetBaseForceField().Get(20, 20) == ForceVector{}, "temporary movement changed base acceleration");

    world.SetCell(40, 30, {CellTypeId::Dirt});
    world.FillBaseForceRegion({32, 24, 24, 24}, {0, 3600});
    spec = Source(40.5, 30.5, 10);
    spec.Acceleration = {0, -3600};
    const auto shelter = world.InstallFieldSource(2, spec);
    for (uint64_t step = 6; step < 9; ++step) {
        Check(world.StepEnvironment(double(step) * FieldResponse::StepSeconds), "counterforce step failed");
        Check(world.GetCell(40, 30).Type == CellTypeId::Dirt && world.GetEnvironment().LastActiveRegions() > 0,
              "active source failed to wake stable materials");
    }
    Check(shelter && world.StopFieldSource(2, *shelter) && world.StepEnvironment(9 * FieldResponse::StepSeconds)
        && world.GetCell(40, 31).Type == CellTypeId::Dirt, "stopping a source did not restore base motion");
}

void CapacityValidationAndResetIdentity() {
    World world(64, 64);
    const auto memory = world.GetMemoryUsage().Get(performance::MemoryComponent::Total);
    for (size_t index = 0; index < FieldSources::Capacity; ++index)
        Check(world.InstallFieldSource(1, Source()).has_value(), "source capacity was smaller than advertised");
    Check(!world.GetEnvironment().GetFieldSources().HasCapacity() && !world.InstallFieldSource(2, Source())
        && world.GetMemoryUsage().Get(performance::MemoryComponent::Total) == memory,
          "capacity rejection allocated memory or changed bounded source storage");
    const auto old = world.GetEnvironment().GetFieldSources().Sources().front().Handle;
    world.StopFieldSources(1);
    Check(world.GetEnvironment().GetFieldSources().HasCapacity(), "owner cleanup failed to reclaim slots");
    const auto last = world.InstallFieldSource(1, Source());
    Check(last && last->Id > old.Id, "installation recycled an old identity");
    world.GenerateTerrain(64);
    const auto reset = world.InstallFieldSource(1, Source());
    Check(reset && reset->Id > last->Id && !world.StopFieldSource(1, *last), "world reset resurrected a stale source handle");

    Invalid([&] { world.InstallFieldSource(0, Source()); }, "zero owner accepted");
    for (const double radius : {0., -1., 16.1, std::numeric_limits<double>::quiet_NaN()}) {
        auto spec = Source(); spec.Radius = radius;
        Invalid([&] { world.InstallFieldSource(1, spec); }, "invalid source radius accepted");
    }
    for (const uint64_t lifetime : {uint64_t{0}, uint64_t{3601}}) {
        auto spec = Source(); spec.Lifetime = lifetime;
        Invalid([&] { world.InstallFieldSource(1, spec); }, "invalid source lifetime accepted");
    }
    for (const ForceVector acceleration : {ForceVector{36000, 1}, ForceVector{std::numeric_limits<float>::infinity(), 0}}) {
        auto spec = Source(); spec.Acceleration = acceleration;
        Invalid([&] { world.InstallFieldSource(1, spec); }, "invalid source acceleration accepted");
    }
}

void DistantCoordinatesAndDomainExit() {
    for (const int64_t origin : {int64_t{1} << 55, -(int64_t{1} << 55)}) {
        World world;
        auto &map = testing::WorldAccess::Map(world);
        map.Insert(std::make_unique<WorldChunk>(ChunkOf({origin, origin})));
        testing::WorldAccess::SimulationBounds(world) = {origin, origin, 64, 64};
        FieldSourceSpec spec{{ChunkOf({origin, origin}), {16.5, 16.5}}, 1, {3600, 0}, 10};
        const auto handle = world.InstallFieldSource(1, spec);
        Check(handle && world.GetForce(origin + 16, origin + 16).X == 3600
            && world.GetForce(origin + 17, origin + 16).X == 3600
            && world.GetForce(origin + 18, origin + 16).X == 0, "distant source rounded adjacent cells");
        spec.Center.Local = {1, 1};
        Check(world.InstallFieldSource(2, spec).has_value(), "disk touching domain edge was refused");
        spec.Center.Local = {.999, 1};
        Check(!world.InstallFieldSource(2, spec), "disk crossing domain edge was admitted");
        testing::WorldAccess::SimulationBounds(world) = {origin + 17, origin, 64, 64};
        Check(world.GetForce(origin + 16, origin + 16).X == 0 && world.GetForce(origin + 17, origin + 16).X == 0,
              "inactive or partially clipped source contributed outside its complete domain");
        Check(world.StepEnvironment(0) && world.GetEnvironment().GetFieldSources().Size() == 0,
              "successful domain step retained an inactive source");
    }
    World missing;
    Check(!missing.InstallFieldSource(1, Source()), "source admitted a missing center chunk");
}

void PendingStepDoesNotAgeOrMutate() {
    const auto directory = std::filesystem::temp_directory_path()
        / ("pixel-field-source-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        World world;
        WorldStreamSettings settings;
        settings.Directory = directory;
        settings.WindowEdge = 256;
        settings.SurfaceY = 10000;
        settings.BudgetBytes = 50ull * 1024 * 1024;
        world.EnableStreaming(settings);
        world.SetCell(32, 32, {CellTypeId::Dirt});
        const auto handle = world.InstallFieldSource(1, Source(32.5, 32.5, 2));
        Check(handle.has_value(), "pending source setup failed");
        const auto revision = world.GetEnvironment().GetFieldSources().Revision();
        world.SetPublicationBytes(settings.BudgetBytes - world.GetStreamer()->RetainedBytes() - 128);
        for (int retry = 0; retry < 2; ++retry)
            Check(!world.StepEnvironment(0) && world.GetEnvironment().IsWaitingForBudget()
                && world.GetEnvironment().GetFieldSources().Sources()[0].Remaining == 2
                && world.GetEnvironment().GetFieldSources().Revision() == revision,
                  "pending retry advanced source lifetime or publication revision");
        for (int mutation = 0; mutation < 5; ++mutation) {
            bool rejected = false;
            try {
                if (mutation == 0) world.InstallFieldSource(1, Source());
                if (mutation == 1) world.UpdateFieldSource(1, *handle, Source());
                if (mutation == 2) world.StopFieldSource(1, *handle);
                if (mutation == 3) world.StopFieldSources(1);
                if (mutation == 4) world.DiscardStreaming();
            } catch (const std::logic_error &) { rejected = true; }
            Check(rejected, "pending step admitted a source mutation");
        }
        world.SetPublicationBytes(0);
        Check(world.StepEnvironment(0) && world.GetEnvironment().GetFieldSources().Sources()[0].Remaining == 1
            && world.GetCell(33, 32).Type == CellTypeId::Dirt, "budget recovery did not commit source effect exactly once");
        Check(world.StepEnvironment(FieldResponse::StepSeconds) && world.GetEnvironment().GetFieldSources().Size() == 0
            && world.GetCell(34, 32).Type == CellTypeId::Dirt, "post-retry source did not expire on its second completed step");
    }
    std::filesystem::remove_all(directory);
}
} // namespace

int main() {
    try {
        OwnershipUpdateAndStableComposition();
        LifetimesWakeAndMaterialMotion();
        CapacityValidationAndResetIdentity();
        DistantCoordinatesAndDomainExit();
        PendingStepDoesNotAgeOrMutate();
        std::cout << "Field source contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
