#include "ProcessFakeHost.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

using namespace pixel::formula::process;
namespace {
void PrintDiagnostic(const Diagnostic &diagnostic) {
    std::cerr << diagnostic.Resource << ':' << diagnostic.Source.Line << ':' << diagnostic.Source.Column
              << ": " << diagnostic.Message << '\n';
}
bool ParseCount(std::string_view text, std::uint64_t &value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
}
int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "usage: PixelFormulaProcessDemo FILE [--steps N] [--hit-at N] [--wake-at N] [--reject]\n";
        return 2;
    }
    std::uint64_t steps = 12, hitAt = std::numeric_limits<std::uint64_t>::max(), wakeAt = hitAt;
    bool reject = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view option = argv[i];
        if (option == "--reject") { reject = true; continue; }
        auto *target = option == "--steps" ? &steps : option == "--hit-at" ? &hitAt : option == "--wake-at" ? &wakeAt : nullptr;
        if (!target || ++i == argc || !ParseCount(argv[i], *target)) {
            std::cerr << "invalid option/value\n";
            return 2;
        }
    }
    if (steps > 100000) { std::cerr << "fake host limits --steps to 100000\n"; return 2; }
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) { std::cerr << "cannot open source\n"; return 2; }
    // Bound input before allocating the source string.
    std::string source;
    for (char c; source.size() <= 16384 && input.get(c);) source.push_back(c);
    if (input.bad()) { std::cerr << "cannot read source\n"; return 2; }
    auto compiled = Compile(argv[1], source);
    if (const auto *error = std::get_if<Diagnostic>(&compiled)) { PrintDiagnostic(*error); return 1; }
    auto artifact = std::get<ProgramArtifact>(std::move(compiled));
    std::cout << "process-v" << artifact.Identity().SemanticsVersion << " sha256=" << artifact.Identity().ContentSha256 << '\n';
    auto started = Instance::Start(artifact, 1);
    if (const auto *error = std::get_if<Diagnostic>(&started)) { PrintDiagnostic(*error); return 1; }
    testing::FakeHost host(std::get<std::unique_ptr<Instance>>(std::move(started)));
    host.RejectEffects = reject;
    std::size_t printed = 0;
    for (std::uint64_t step = 0; step < steps; ++step) {
        if (step == hitAt) host.Hit({7, 0, 0, 0});
        if (step == wakeAt) host.Wake();
        const auto result = host.Advance();
        for (; printed < host.Trace().size(); ++printed) {
            const auto &entry = host.Trace()[printed];
            std::cout << "t=" << entry.At << ' ' << (std::holds_alternative<Dig>(entry.Request.Description) ? "dig" :
                std::holds_alternative<Wind>(entry.Request.Description) ? "wind" : "bolt")
                << " cell=" << entry.Request.Origin.CellX << ',' << entry.Request.Origin.CellY
                << (entry.Result == Outcome::Rejected ? " rejected" : " applied") << '\n';
        }
        if (result.Fault) { PrintDiagnostic(*result.Fault); return 1; }
        if (result.End) break;
    }
    host.Cancel();
    std::cout << "inventory=" << host.Inventory << " owned=" << host.OwnedCount() << " pending=" << host.PendingCount() << '\n';
    return 0;
}
