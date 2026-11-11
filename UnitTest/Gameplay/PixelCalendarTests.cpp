#include "Runtime/Render/CalendarLighting.h"
#include "Runtime/Render/GamePresentation.h"
#include "Runtime/Gameplay/UI/InventoryUI.h"
#include "Runtime/Simulation/World/WorldStorageError.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace pixel;
namespace {
void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}

template<class Action> void Reject(const Action &action) {
    try { action(); }
    catch (const std::invalid_argument &) { return; }
    catch (const std::overflow_error &) { return; }
    throw std::runtime_error("invalid calendar operation accepted");
}

void BoundariesAndLighting() {
    WorldCalendar calendar;
    Require(calendar.Snapshot().CurrentSeason == Season::Spring && calendar.Snapshot().Hour == 8,
            "new world does not begin on a spring morning");
    auto state = calendar.State();
    state.Day = 6;
    state.TickOfDay = 71999;
    calendar.Restore(state);
    const auto before = calendar.Snapshot();
    calendar.Advance();
    Require(calendar.State().Day == 7 && calendar.State().TickOfDay == 0
        && calendar.Snapshot().CurrentSeason == Season::Summer && calendar.Snapshot().DayOfSeason == 1,
        "midnight did not change season exactly once");
    Require(std::abs(before.SunriseHour - calendar.Snapshot().SunriseHour) < .001f,
            "day length jumped at the season boundary");
    calendar.Advance(21ull * 72000);
    Require(calendar.Snapshot().Year == 2 && calendar.Snapshot().CurrentSeason == Season::Spring,
            "winter did not roll over to a new year");
    state = {13, 123, 600, 3, 7};
    calendar.Restore(state);
    WorldCalendar reference;
    reference.Restore(state);
    calendar.Advance(123456);
    for (int i = 0; i < 123456; ++i) reference.Advance();
    Require(calendar.State() == reference.State(), "batched advance differs from fixed steps");

    std::array<PostProcessParameters, 4> seasonal;
    for (uint32_t season = 0; season < 4; ++season) {
        calendar.Restore({season * 7, 36000, 1200, 7, 1});
        const auto noon = calendar.Snapshot();
        seasonal[season] = CalendarLighting(noon);
        Require(noon.Phase == DayPhase::Day && noon.Daylight == 1, "noon is not daylight");
        calendar.Restore({season * 7, 0, 1200, 7, 1});
        const auto midnight = calendar.Snapshot();
        Require(midnight.Phase == DayPhase::Night && midnight.Daylight == 0, "midnight is not night");
        for (size_t channel = 0; channel < 3; ++channel)
            Require(CalendarLighting(midnight).Values[channel] < seasonal[season].Values[channel], "night did not darken scene");
        calendar.Restore({season * 7, uint32_t(noon.SunriseHour * 3000), 1200, 7, 1});
        Require(calendar.Snapshot().Phase == DayPhase::Dawn && std::abs(calendar.Snapshot().Daylight - .5f) < .001f,
                "sunrise transition missing");
    }
    Require(seasonal[0].Values != seasonal[1].Values && seasonal[1].Values != seasonal[2].Values
        && seasonal[2].Values != seasonal[3].Values, "season colours are indistinguishable");
    calendar.Restore({7, 36000, 1200, 7, 1});
    Require(calendar.Snapshot().SunsetHour - calendar.Snapshot().SunriseHour == 14, "summer day is not fourteen hours");
    calendar.Restore({21, 36000, 1200, 7, 1});
    Require(calendar.Snapshot().SunsetHour - calendar.Snapshot().SunriseHour == 10, "winter day is not ten hours");

    const auto valid = calendar.State();
    for (int invalid = 0; invalid < 8; ++invalid) {
        auto bad = valid;
        switch (invalid) {
        case 0: bad.SecondsPerDay = 0; break;
        case 1: bad.SecondsPerDay = 86401; break;
        case 2: bad.DaysPerSeason = 0; break;
        case 3: bad.DaysPerSeason = 366; break;
        case 4: bad.TimeScale = 0; break;
        case 5: bad.TimeScale = 3601; break;
        case 6: bad.TickOfDay = 72000; break;
        case 7: bad.Day = UINT64_MAX; break;
        }
        Reject([&] { calendar.Restore(bad); });
        Require(calendar.State() == valid, "failed restore mutated calendar");
    }
    Reject([&] { calendar.Advance(UINT64_MAX); });
    Require(calendar.State() == valid, "overflowing advance mutated calendar");
    calendar.Restore({UINT64_MAX - 1, 71999, 1200, 7, 1});
    Reject([&] { calendar.Advance(); });
}

void PhaseTargets() {
    WorldCalendar calendar;
    for (const auto days : {1u, 2u, 7u, 365u}) {
        for (const auto seconds : {1u, 7u, 1200u, 86400u}) {
            const auto ticksPerDay = seconds * WorldCalendar::TicksPerSecond;
            for (uint64_t day = 0; day < 4 * days; ++day) {
                const WorldCalendarState state{day, 0, seconds, days, 1};
                calendar.Restore(state);
                Require(calendar.TickAtPhase(DayPhase::Night) == 0
                    && calendar.TickAtPhase(DayPhase::Day) == ticksPerDay / 2, "midnight/noon target changed");
                for (const auto phase : {DayPhase::Dawn, DayPhase::Dusk}) {
                    const auto target = calendar.TickAtPhase(phase);
                    Require(calendar.State() == state, "phase query mutated the calendar");
                    auto moved = calendar;
                    auto candidate = state;
                    candidate.TickOfDay = target;
                    moved.Restore(candidate);
                    const auto snapshot = moved.Snapshot();
                    Require(snapshot.Phase == phase, "phase target missed dawn/dusk during seasonal blending");
                    const auto horizon = phase == DayPhase::Dawn ? snapshot.SunriseHour : snapshot.SunsetHour;
                    const auto error = std::abs(snapshot.DayProgress * 24 - horizon);
                    Require(error <= 24.0 / ticksPerDay + .00001, "phase target is not at its own sunrise/sunset");
                    Require(moved.TickAtPhase(phase) == target, "repeated phase jump drifted");
                    candidate.TickOfDay = ticksPerDay - 1;
                    moved.Restore(candidate);
                    Require(moved.TickAtPhase(phase) == target, "phase target depends on starting time");
                    for (const auto neighbor : {target - 1, target + 1}) {
                        candidate.TickOfDay = neighbor;
                        moved.Restore(candidate);
                        const auto nearby = moved.Snapshot();
                        const auto nearbyHorizon = phase == DayPhase::Dawn ? nearby.SunriseHour : nearby.SunsetHour;
                        Require(error <= std::abs(nearby.DayProgress * 24 - nearbyHorizon) + .000001,
                                "phase target skipped a closer representable tick");
                    }
                }
            }
        }
    }
    calendar.Restore({6, 0, 1200, 7, 1}); // review regression: spring 7 at midnight
    auto dusk = calendar.State();
    dusk.TickOfDay = calendar.TickAtPhase(DayPhase::Dusk);
    calendar.Restore(dusk);
    Require(calendar.Snapshot().Phase == DayPhase::Dusk && calendar.Snapshot().Daylight > .49f
        && calendar.Snapshot().Daylight < .51f, "default spring-end dusk is still full daylight");
    Reject([&] { calendar.TickAtPhase(static_cast<DayPhase>(99)); });
}

void SessionAndPresentation(bool threaded) {
    GameSession session;
    if (threaded) session.EnableSimulationThread();
    GameCamera camera;
    camera.SetFramebufferSize({512, 384});
    GamePresentation presentation;
    auto publish = presentation.CreateFramePublisher(session, camera);
    WorldGridFrame frame;
    publish(frame);
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {480, 480, 64, 64});
    const auto start = session.GetCalendar().State();
    session.Control(NativeCaller::Driver, api::SetPaused{true});
    session.Tick(.25f, {});
    Require(session.GetCalendar().State() == start, "pause advanced calendar");
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(.25f, {});
    Require(session.GetCalendar().State().TickOfDay == start.TickOfDay + 1 && view->Stamp().Tick == 1,
            "single step did not advance calendar exactly once");
    WorldCalendarState winter{21, 0, 1200, 7, 60};
    const auto stamp = session.CurrentCommit();
    Require(session.Control(NativeCaller::Driver, api::SetWorldCalendar{winter}).Succeeded(), "calendar control failed");
    Require(session.CurrentCommit().Tick == stamp.Tick && session.CurrentCommit().Revision == stamp.Revision + 1,
            "calendar edit changed physics time or failed to publish");
    Require(view->Calendar().CurrentSeason == Season::Winter && view->Calendar().Hour == 0,
            "committed view did not publish calendar edit");
    const auto edited = session.CurrentCommit();
    session.Control(NativeCaller::Driver, api::SetWorldCalendar{winter});
    Require(session.CurrentCommit() == edited, "no-op calendar edit published a revision");
    auto bad = winter;
    bad.DaysPerSeason = 0;
    Require(session.Control(NativeCaller::Driver, api::SetWorldCalendar{bad}).Rejection == api::Reason::InvalidArgument
        && session.GetCalendar().State() == winter, "bad calendar input mutated session");
    Require(session.Control(NativeCaller::Input, api::SetWorldCalendar{}).Rejection == api::Reason::PermissionDenied
        && session.Control(session.WorldWillSession(), api::SetWorldCalendar{}).Rejection == api::Reason::PermissionDenied,
        "calendar control bypassed native authorization");
    publish(frame);
    Require(frame.Post.Values == CalendarLighting(view->Calendar(), presentation.Post).Values,
            "material scene lost calendar lighting");
    ui::InventoryUI ui;
    const auto &gameplay = presentation.PublishedGameplay();
    const auto &calendar = presentation.PublishedCalendar();
    ui.Publish(&*gameplay, session.PlayerInventory(), &*calendar);
    Require(ui.Read().CalendarLabel == "Winter 1 / 00:00", "native HUD does not display calendar");
    const auto revision = ui.Paint().Revision;
    ui.Publish(&*gameplay, session.PlayerInventory(), &*calendar);
    Require(ui.Paint().Revision == revision, "unchanged calendar caused redundant HUD upload");
    presentation.DisplayMode = GridDisplayMode::ForceField;
    publish(frame);
    Require(frame.Post.Values == presentation.Post.Values, "calendar tinted diagnostic field colours");
    session.Control(NativeCaller::Driver, api::SetPaused{false});
    session.Tick(1.f / 60, {});
    Require(session.GetCalendar().State().TickOfDay == 60 && session.CurrentCommit().Tick == 2,
            "calendar speed accelerated physics or was ignored");
    Require(presentation.PublishedCalendar()->Hour == 0 && presentation.PublishedCalendar()->Minute == 0,
            "render snapshot changed without publication");
    if (threaded) {
        const auto beforeQueue = session.GetCalendar().State();
        Require(session.Execute(NativeCaller::Driver, api::MineRegion{{520, 512}}).Queued(), "calendar queue fixture failed");
        Require(session.Control(NativeCaller::Driver, api::SetWorldCalendar{}).Status == api::ControlStatus::Deferred
            && session.GetCalendar().State() == beforeQueue, "calendar edit bypassed queued world changes");
    }
    session.Control(NativeCaller::Driver, api::ResetWorld{});
    Require(session.GetCalendar().State() == WorldCalendarState{}, "new world did not reset calendar");
}

struct TemporaryRoot {
    std::filesystem::path Path = std::filesystem::temp_directory_path()
        / ("pixel-calendar-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryRoot() { std::filesystem::remove_all(Path); }
};

struct EnvironmentVariable {
    std::string Name;
    std::optional<std::string> Previous;
    static void Set(const std::string &name, const char *value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value ? value : "");
#else
        if (value) setenv(name.c_str(), value, 1); else unsetenv(name.c_str());
#endif
    }
    EnvironmentVariable(std::string name, const std::string &value) : Name(std::move(name)) {
        if (const auto *old = std::getenv(Name.c_str())) Previous = old;
        Set(Name, value.c_str());
    }
    ~EnvironmentVariable() { Set(Name, Previous ? Previous->c_str() : nullptr); }
};

void PersistenceAndCorruption() {
    TemporaryRoot files;
    EnvironmentVariable root("PIXEL_SAVE_ROOT", files.Path.string()), window("PIXEL_SIMULATION_WINDOW", "256");
    const WorldCalendarState saved{27, 35999, 600, 7, 1};
    {
        GameSession session(true);
        session.Control(NativeCaller::Driver, api::SetPaused{true});
        Require(session.Control(NativeCaller::Driver, api::SetWorldCalendar{saved}).Succeeded(), "saved calendar control failed");
        Require(session.Control(NativeCaller::Driver, api::SaveWorld{}).Succeeded(), "calendar checkpoint failed");
    }
    {
        GameSession restored(true);
        Require(restored.IsPaused() && restored.GetCalendar().State() == saved, "checkpoint lost calendar or global pause");
        restored.Control(NativeCaller::Driver, api::StepSimulation{});
        restored.Tick(1.f / 60, {});
        Require(restored.GetCalendar().State().Day == 28 && restored.GetCalendar().Snapshot().Year == 2,
                "restored calendar did not continue through midnight");
    }
    SessionStore store(files.Path / "codec");
    const auto directory = store.CreateWorldDirectory({});
    SessionSaveData data;
    data.Calendar = saved;
    store.WriteMetadata(directory, data);
    Require(store.ReadMetadata(directory).Calendar == saved, "metadata lost custom day length or fractional day");
    std::ifstream in(directory / "world.meta");
    const std::string original{std::istreambuf_iterator<char>(in), {}};
    for (const auto *invalid : {"27 36000 600 7 1\n", "27 0 600 0 1\n", "27 0 600 7 0\n", "27 0 600\n",
                              "-2 0 600 7 1\n", "27 0 600 7 1 extra\n"}) {
        const auto lastLine = original.rfind('\n', original.size() - 2) + 1;
        { std::ofstream out(directory / "world.meta"); out << original.substr(0, lastLine) << invalid; }
        Require(storage::CaptureFailure([&] { store.ReadMetadata(directory); }).has_value(), "corrupt calendar was accepted");
    }
}
} // namespace

int main() {
    try {
        BoundariesAndLighting();
        PhaseTargets();
        SessionAndPresentation(false);
        SessionAndPresentation(true);
        PersistenceAndCorruption();
        std::cout << "Calendar: boundaries, lighting, committed frames, worker, controls, HUD and checkpoints passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
