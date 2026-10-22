#include "../Support/TestAccess.h"
#include "LLMView/JsonAdapter.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>

namespace {
using namespace pixel;
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
constexpr uint32_t Frames = 128;

double Milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
double Percentile(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[(values.size() * 95 + 99) / 100 - 1];
}
double Mean(const std::vector<double> &values) {
    return std::accumulate(values.begin(), values.end(), 0.) / values.size();
}

void Measure(std::ostream &output, bool dynamicWind, bool queries) {
    GameSession session;
    if (dynamicWind) {
        WindStrategySettings wind;
        wind.Kind = WindStrategyKind::Procedural;
        wind.Coarse.Seed = 0x4d325f35;
        testing::SessionAccess::World(session).ConfigureWindDomain(424, wind);
        testing::SessionAccess::PublishFixtureChanges(session);
    }
    session.Control(NativeCaller::Driver, api::SetPaused{false});
    llm::ViewService views(session);
    std::array<llm::ObserverId, 3> observers;
    for (auto &observer : observers) observer = views.RegisterObserver(llm::Policy::FullAccess());
    std::vector<double> frames, captures, reads;
    uint64_t peakBytes = 0, jsonBytes = 0;
    for (uint32_t frame = 0; frame < Frames; ++frame) {
        const auto frameStart = Clock::now();
        views.AdvanceServiceFrame();
        session.Tick(1.f / 60, {frame % 48 < 24 ? 1 : -1, frame % 60 == 0});
        double captureMilliseconds = 0, queryMilliseconds = 0;
        if (queries) {
            for (size_t i = 0; i < observers.size(); ++i) {
                const api::Rect scope{{480 + int64_t(i) * 16, 200}, 64, 64};
                const auto captureStart = Clock::now();
                const auto opened = views.OpenView(observers[i], scope);
                captureMilliseconds += Milliseconds(captureStart);
                if (!opened.View) throw std::runtime_error("benchmark open failed");
                const auto queryStart = Clock::now();
                llm::CursorId cursor = 0;
                do {
                    const auto prepared = views.Query(observers[i], opened.View->Id, {api::Topic::Materials, scope}, cursor);
                    if (prepared.Result != llm::Status::Ok) throw std::runtime_error("benchmark query failed");
                    peakBytes = std::max(peakBytes, views.Statistics().RetainedBytes);
                    auto response = views.Deliver(observers[i], prepared.Delivery);
                    if (!response.Page) throw std::runtime_error("benchmark deliver failed");
                    cursor = response.Page->NextCursor;
                    jsonBytes += llm::ToJson(response).dump().size();
                } while (cursor);
                const auto prepared = views.Query(observers[i], opened.View->Id, {api::Topic::AirVelocity, scope, 16});
                auto wind = views.Deliver(observers[i], prepared.Delivery);
                if (!wind.Page || wind.Page->WindSummaries.empty()) throw std::runtime_error("benchmark summary failed");
                if (frame == 0 && dynamicWind && (!wind.Page->Wind || wind.Page->Wind->Backend != api::WindKind::Procedural
                    || wind.Page->WindSummaries[0].AirSamples == 0))
                    throw std::runtime_error("benchmark did not sample dynamic air");
                jsonBytes += llm::ToJson(wind).dump().size();
                queryMilliseconds += Milliseconds(queryStart);
            }
        }
        frames.push_back(Milliseconds(frameStart));
        captures.push_back(captureMilliseconds);
        reads.push_back(queryMilliseconds);
        if (session.ObservationStats().ActiveScopes != 0)
            throw std::runtime_error("external capture cache retained a live scope");
    }
    for (auto observer : observers) views.RemoveObserver(observer);
    const auto released = views.Statistics();
    if (released.ActiveViews || released.PendingDeliveries) throw std::runtime_error("benchmark view storage was not released");
    output << (dynamicWind ? "dynamic_procedural" : "static_empty") << ',' << (queries ? "three_observers_json" : "baseline")
        << ',' << Frames << ',' << Mean(frames) << ',' << Percentile(frames)
        << ',' << Mean(captures) << ',' << Percentile(captures) << ',' << Mean(reads) << ',' << Percentile(reads)
        << ',' << peakBytes << ',' << released.RetainedBytes << ',' << jsonBytes << '\n';
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) { std::cerr << "usage: PixelLLMViewBenchmark output.csv\n"; return 2; }
    std::ofstream output(argv[1]);
    if (!output) throw std::runtime_error("cannot open benchmark output");
    output << std::fixed << std::setprecision(6)
        << "scenario,mode,frames,mean_frame_ms,p95_frame_ms,mean_open_ms,p95_open_ms,mean_query_json_ms,p95_query_json_ms,peak_view_bytes,released_service_bytes,json_bytes\n";
    for (bool dynamicWind : {false, true}) {
        Measure(output, dynamicWind, false);
        Measure(output, dynamicWind, true);
    }
}
