#include "BehaviorDecision.h"
#include <algorithm>
#include <chrono>
#include <iostream>

using namespace pixel;
using namespace pixel::experiment;
namespace {
constexpr size_t Samples = 2000, Batch = 32;
uint64_t Checksum = 0;

llm::ModelInput Input(bool device, bool visible) {
    llm::ModelInput input;
    input.Id = 1;
    input.Intent.Target = {63, 63};
    input.Intent.Scope = {{0, 0}, 64, 64};
    input.Intent.Prayer = "Help the garden";
    for (int64_t y = 0; y < 64; ++y)
        for (int64_t x = 0; x < 64; ++x) input.Materials.push_back({{x, y}, 0});
    for (uint64_t id = 1; id <= llm::ModelLimits::Devices; ++id) {
        api::DeviceObservation observed;
        observed.Ref = {id, 1};
        input.Devices.push_back(observed);
    }
    if (device) input.Intent.Device = visible ? 64 : 65;
    else if (!visible) input.Materials.back().MaterialId.reset();
    return input;
}

template<class Evaluate>
void Measure(std::string_view backend, std::string_view scenario, const llm::ModelInput &input,
             size_t artifactBytes, Evaluate evaluate) {
    for (size_t i = 0; i < 100; ++i) Checksum += uint64_t(evaluate(input).Choice.Kind);
    std::vector<double> times;
    times.reserve(Samples);
    for (size_t i = 0; i < Samples; ++i) {
        const auto start = std::chrono::steady_clock::now();
        for (size_t j = 0; j < Batch; ++j) {
            const auto reply = evaluate(input);
            Checksum += reply.Id + reply.Choice.DeviceId + reply.Explanation.size() + uint64_t(reply.Choice.Kind);
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        times.push_back(std::chrono::duration<double, std::nano>(elapsed).count() / Batch);
    }
    std::sort(times.begin(), times.end());
    std::cout << backend << ',' << scenario << ',' << Samples << ',' << Batch << ','
        << times[Samples / 2] << ',' << times[Samples * 95 / 100] << ','
        << input.RetainedBytes() << ',' << artifactBytes << '\n';
}
} // namespace

int main() {
    const auto graph = AidDecisionGraph(); // Compilation excluded from repeated evaluation.
    std::cout << "backend,scenario,samples,batch,p50_ns,p95_ns,input_retained_bytes,artifact_retained_bytes\n";
    for (const bool device : {false, true}) {
        for (const bool visible : {true, false}) {
            const auto input = Input(device, visible);
            const auto scenario = device ? (visible ? "device-last" : "device-missing")
                                         : (visible ? "cell-last" : "cell-unknown");
            Measure("native", scenario, input, 0, NativeAidDecision);
            Measure("graph", scenario, input, graph.RetainedBytes(), [&](const auto &value) { return graph.Evaluate(value); });
        }
    }
    std::cerr << "checksum=" << Checksum << '\n';
}
