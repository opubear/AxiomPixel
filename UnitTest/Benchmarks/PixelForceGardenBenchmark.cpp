#include "../Support/TestAccess.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace pixel;
using Clock = std::chrono::steady_clock;
constexpr int Rounds = 3;
constexpr int StepsPerRound = 240;

double Percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const auto index = size_t(std::ceil(fraction * values.size())) - 1;
    return values.at(index);
}

struct Result {
    bool DeviceEnabled = false;
    int Round = 0;
    double P50 = 0, P95 = 0;
    uint64_t MeanActiveSources = 0, PeakRetainedSources = 0, PeakPrograms = 0;
    uint64_t WorldReservedBytes = 0, GameplayProjectionBytes = 0;
    uint32_t WildGrowth = 0, HomeGrowth = 0;
};

Result Measure(bool enabled, int round) {
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    generation.Seed = 0x47415244454e;
    GameSession session(false, generation);
    if (!session.Control(NativeCaller::Driver, api::CreateForceGarden{}).Succeeded())
        throw std::runtime_error("benchmark garden creation failed");

    auto &world = testing::SessionAccess::World(session);
    testing::SessionAccess::PublishFixtureChanges(session);
    if (!session.Control(NativeCaller::Driver, api::ClearRunning{}).Succeeded())
        throw std::runtime_error("could not stop natural crystal for device comparison");

    auto &inventory = testing::SessionAccess::Inventory(session);
    const std::array stock{items::ItemAmount{items::ItemId::Stone, 4},
                           items::ItemAmount{items::ItemId::CopperOre, 2},
                           items::ItemAmount{items::ItemId::Coal, 16}};
    if (inventory.Add(inventory.Reference(session.PlayerInventory()), stock) != items::Result::Applied)
        throw std::runtime_error("benchmark stock fixture failed");

    const auto garden = [&] {
        auto view = session.CreateReadView();
        session.RefreshReadView(*view, world.GetSimulationBounds());
        if (!view->Gameplay().Garden) throw std::runtime_error("benchmark garden projection missing");
        return *view->Gameplay().Garden;
    }();
    const auto placed = session.Execute(NativeCaller::Driver,
        api::PlaceDevice{{GardenDeviceX, garden.State.SurfaceY - 2}});
    if (!placed.Applied()) throw std::runtime_error("benchmark device placement failed");
    auto device = std::get<api::DeviceChanged>(placed.Result).Device;
    if (!session.Execute(NativeCaller::Driver, api::SupplyDevice{device.Ref, 16}).Applied())
        throw std::runtime_error("benchmark device supply failed");
    device = session.GetDevices().Find(device.Ref.Id)->Value;
    if (!session.Execute(NativeCaller::Driver, api::SetDeviceEnabled{device.Ref, enabled}).Applied())
        throw std::runtime_error("benchmark device state change failed");
    if (!session.Control(NativeCaller::Driver, api::SetPaused{false}).Succeeded())
        throw std::runtime_error("benchmark unpause failed");

    // Warm up the same fixed scene, then collect the measured 240 committed steps.
    for (int i = 0; i < 8; ++i) {
        session.BeginFrame();
        session.Tick(1.f / 60.f, {});
    }
    std::vector<double> samples;
    samples.reserve(StepsPerRound);
    uint64_t activeSourceSteps = 0, retainedSourcePeak = 0, processPeak = 0;
    for (int i = 0; i < StepsPerRound; ++i) {
        session.BeginFrame();
        const auto start = Clock::now();
        session.Tick(1.f / 60.f, {});
        samples.push_back(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
        const auto sources = world.GetEnvironment().GetFieldSources().Sources();
        const auto active = std::count_if(sources.begin(), sources.end(), [&](const auto &source) {
            return !source.Suspended && world.GetEnvironment().GetFieldSources().Contributes(source.Handle, world.GetSimulationBounds());
        });
        activeSourceSteps += active;
        retainedSourcePeak = std::max<uint64_t>(retainedSourcePeak, sources.size());
        processPeak = std::max<uint64_t>(processPeak, session.GetFormulaRuntime().InspectProcesses().size());
        if (enabled && !active)
            throw std::runtime_error("enabled benchmark device had no active source during a measured step");
    }

    Result result;
    result.DeviceEnabled = enabled;
    result.Round = round;
    result.P50 = Percentile(samples, .50);
    result.P95 = Percentile(samples, .95);
    result.PeakRetainedSources = retainedSourcePeak;
    result.MeanActiveSources = activeSourceSteps / StepsPerRound;
    result.PeakPrograms = processPeak;
    result.WorldReservedBytes = world.GetMemoryUsage().Get(performance::MemoryComponent::Total).ReservedBytes();
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, world.GetSimulationBounds());
    result.GameplayProjectionBytes = view->Gameplay().RetainedBytes();
    result.WildGrowth = view->Gameplay().Garden->State.WildGrowth;
    result.HomeGrowth = view->Gameplay().Garden->State.HomeGrowth;
    return result;
}

void Write(std::ostream &out, const Result &result) {
    out << (result.DeviceEnabled ? "on" : "off") << ',' << result.Round << ',' << StepsPerRound << ','
        << std::fixed << std::setprecision(6) << result.P50 << ',' << result.P95 << ','
        << result.MeanActiveSources << ',' << result.PeakRetainedSources << ',' << result.PeakPrograms << ','
        << result.WorldReservedBytes << ',' << result.GameplayProjectionBytes << ','
        << result.WildGrowth << ',' << result.HomeGrowth << '\n';
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::ofstream file;
        std::ostream *out = &std::cout;
        if (argc > 1) {
            file.open(std::filesystem::path(argv[1]), std::ios::out | std::ios::trunc);
            if (!file) throw std::runtime_error("could not open benchmark CSV output");
            out = &file;
        }
        *out << "device,round,steps,p50_ms,p95_ms,mean_active_sources,peak_retained_sources,peak_programs,world_reserved_bytes,"
                "gameplay_projection_bytes,wild_growth,home_growth\n";
        for (const bool enabled : {false, true})
            for (int round = 1; round <= Rounds; ++round)
                Write(*out, Measure(enabled, round));
        std::cerr << "fixture: finite Flat 1024x1024 world, 8 warmup steps, 16 coal supplied per run; "
                     "mean active sources and peak retained sources/programs over 3 rounds x 240 measured steps per state\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
