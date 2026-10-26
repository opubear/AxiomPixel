#include "../Support/TestAccess.h"
#include "Runtime/Render/PixelWorldPresentation.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <numeric>

using namespace pixel;
namespace {
using Clock = std::chrono::steady_clock;
double Micros(Clock::time_point start) { return std::chrono::duration<double, std::micro>(Clock::now() - start).count(); }
double Median(std::vector<double> values) { std::sort(values.begin(), values.end()); return values[values.size() / 2]; }

void Measure(WindStrategyKind kind, int readers, bool moving) {
    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    WindStrategySettings settings;
    settings.Kind = kind;
    world.ConfigureWindDomain(512, settings);
    if (moving) world.FillRegion({128, 32, 128, 64}, {CellTypeId::Dirt});
    testing::SessionAccess::PublishFixtureChanges(session);
    std::vector<std::shared_ptr<CommittedWorldView>> views;
    std::vector<WorldGridFrame> frames(static_cast<size_t>(readers));
    for (int i = 0; i < readers; ++i) {
        auto view = session.CreateReadView();
        session.RefreshReadView(*view, {0, 0, 512, 512});
        views.push_back(std::move(view));
        frames[size_t(i)].SetRegion({0, 0}, 512, 512);
    }
    session.Control(NativeCaller::Driver, api::SetPaused{false});
    std::vector<double> steps, draws, totals;
    uint64_t checksum = 0;
    for (int frame = 0; frame < 72; ++frame) {
        const auto begin = Clock::now();
        session.Tick(1.f / 60, {});
        const auto step = Micros(begin);
        const auto drawBegin = Clock::now();
        for (int i = 0; i < readers; ++i) {
            BuildForceFieldFrame(*views[size_t(i)], frames[size_t(i)]);
            checksum += frames[size_t(i)].Cells()[100 * 512 + 100];
        }
        const auto draw = Micros(drawBegin);
        if (frame >= 8) { steps.push_back(step); draws.push_back(draw); totals.push_back(Micros(begin)); }
    }
    std::cout << int(kind) << ',' << readers << ',' << moving << ',' << Median(steps) << ',' << Median(draws) << ','
              << Median(totals) << ',' << world.GetMemoryUsage().Get(performance::MemoryComponent::Total).ReservedBytes()
              << ',' << checksum << '\n';
}
}

int main() {
    std::cout << std::fixed << std::setprecision(3)
              << "wind_kind,readers,moving,median_commit_us,median_draw_us,median_total_us,reserved_bytes,checksum\n";
    for (const auto kind : {WindStrategyKind::Empty, WindStrategyKind::Coarse, WindStrategyKind::Procedural})
        for (int readers : {0, 1, 3}) Measure(kind, readers, false);
    Measure(WindStrategyKind::Empty, 1, true);
}
