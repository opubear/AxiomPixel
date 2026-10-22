#include "../Support/TestAccess.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace pixel;
using Clock = std::chrono::steady_clock;
constexpr uint32_t SampleEdge = 64;
constexpr int DefaultTicks = 128;

struct Measurement {
    std::string Scenario;
    bool Projection = false;
    std::vector<double> FrameMilliseconds;
    api::ObservationMetrics Metrics;
};

double Percentile(std::vector<double> samples, double percentile) {
    std::sort(samples.begin(), samples.end());
    const auto index = size_t(std::ceil(percentile * samples.size())) - 1;
    return samples.at(index);
}

void ConfigureScenario(GameSession &session, bool dynamicWind) {
    if (dynamicWind) {
        WindStrategySettings settings;
        settings.Kind = WindStrategyKind::Procedural;
        settings.Coarse.Seed = 0x4d325f34;
        testing::SessionAccess::World(session).ConfigureWindDomain(424, settings);
        testing::SessionAccess::PublishFixtureChanges(session);
    }
    session.Control(NativeCaller::Driver, api::SetPaused{false});
}

Measurement Run(std::string scenario, bool dynamicWind, bool projection, int ticks) {
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    generation.Seed = 0x20261022;
    generation.SurfaceY = 512;
    GameSession session(false, generation);
    ConfigureScenario(session, dynamicWind);

    Measurement measurement;
    measurement.Scenario = std::move(scenario);
    measurement.Projection = projection;
    measurement.FrameMilliseconds.reserve(size_t(ticks));
    const api::Rect scope{{480, 200}, SampleEdge, SampleEdge};
    for (int tick = 0; tick < ticks; ++tick) {
        const PlayerInput input{tick % 48 < 24 ? 1 : -1, tick % 60 == 0};
        const auto start = Clock::now();
        session.Tick(1.0f / 60.0f, input);
        std::optional<api::ObservationSnapshot> exported;
        if (projection) {
            auto result = session.Observe(NativeCaller::Driver, scope);
            if (result.Status != api::ObservationStatus::Available || !result.Snapshot)
                throw std::runtime_error("benchmark observation unavailable");
            exported = std::move(result.Snapshot);
        }
        const auto end = Clock::now();
        measurement.FrameMilliseconds.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        if (tick == 0 && dynamicWind && projection) {
            const auto hasDynamicAirSample = std::any_of(exported->Cells.begin(), exported->Cells.end(),
                [](const auto &cell) { return cell.AirKind == api::AirSampleKind::Sampled; });
            if (!hasDynamicAirSample)
                throw std::runtime_error("dynamic wind benchmark scope contains no sampled air cell");
        }
    }
    measurement.Metrics = session.ObservationStats();
    if (dynamicWind && projection && measurement.Metrics.WindCellsSampled == 0)
        throw std::runtime_error("dynamic wind benchmark scope did not sample any air velocities");
    return measurement;
}

void WriteHeader(std::ostream &out) {
    out << "scenario,mode,ticks,p50_frame_ms,p95_frame_ms,cell_bytes_written,exported_bytes,"
           "retained_bytes,max_retained_bytes,active_scopes\n";
}

void WriteMeasurement(std::ostream &out, const Measurement &measurement) {
    out << measurement.Scenario << ',' << (measurement.Projection ? "projected" : "baseline") << ','
        << measurement.FrameMilliseconds.size() << ',' << std::fixed << std::setprecision(6)
        << Percentile(measurement.FrameMilliseconds, .50) << ','
        << Percentile(measurement.FrameMilliseconds, .95) << ','
        << measurement.Metrics.CellBytesWritten << ',' << measurement.Metrics.ExportedBytes << ','
        << measurement.Metrics.RetainedBytes << ',' << measurement.Metrics.MaximumRetainedBytes << ','
        << measurement.Metrics.ActiveScopes << '\n';
}
} // namespace

int main(int argc, char **argv) {
    try {
        const int ticks = argc > 2 ? std::stoi(argv[2]) : DefaultTicks;
        if (ticks < 64 || ticks > 4096) throw std::invalid_argument("tick count must be 64..4096");
        std::ofstream file;
        std::ostream *out = &std::cout;
        if (argc > 1) {
            file.open(std::filesystem::path(argv[1]), std::ios::out | std::ios::trunc);
            if (!file) throw std::runtime_error("could not open benchmark output");
            out = &file;
        }
        WriteHeader(*out);
        for (const bool dynamicWind : {false, true}) {
            const std::string scenario = dynamicWind ? "dynamic_wind" : "static_wind";
            WriteMeasurement(*out, Run(scenario, dynamicWind, false, ticks));
            WriteMeasurement(*out, Run(scenario, dynamicWind, true, ticks));
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
