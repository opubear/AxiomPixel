#include "../Support/TestAccess.h"
#include "../Support/CheckpointFiles.h"
#include "PixelSmokeApplication.h"
#include "Engine.h"
#include "HAL/Platform/PlatformInterface.h"
#include "InputManager.h"
#include "imgui_internal.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <thread>

namespace {
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

struct SaveRoot {
    std::filesystem::path Path = std::filesystem::temp_directory_path()
        / ("pixel-close-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::optional<std::string> Previous;
    static void Set(const char *value) {
#if defined(_WIN32)
        _putenv_s("PIXEL_SAVE_ROOT", value ? value : "");
#else
        if (value) setenv("PIXEL_SAVE_ROOT", value, 1);
        else unsetenv("PIXEL_SAVE_ROOT");
#endif
    }
    SaveRoot() {
        if (const auto *old = std::getenv("PIXEL_SAVE_ROOT")) Previous = old;
        Set(Path.string().c_str());
    }
    ~SaveRoot() {
        Set(Previous ? Previous->c_str() : nullptr);
        std::filesystem::remove_all(Path);
    }
};

std::string Read(const std::filesystem::path &path) {
    std::ifstream in(path);
    in.exceptions(std::ios::failbit | std::ios::badbit);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void Draw(Axiom &app) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        app.Engine->GetPlatform()->PollEvents();
        if (!app.FrameBegin()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        app.Engine->GetInput()->Dispatch();
        app.Tick(0); // Leave the deliberately pending step to the explicit retry.
        app.FrameEnd();
        return;
    }
    throw std::runtime_error("close smoke could not draw a frame");
}

void CloseAtSaveBoundary() {
    SaveRoot root;
    PixelSmokeApplication app(std::make_unique<PixelGame>(true), {true});
    app.Initialize();
    app.Begin();
    auto &game = static_cast<PixelGame &>(*app.GetGame());
    auto &session = game.GetSession();
    auto &world = pixel::testing::SessionAccess::World(session);
    auto *platform = app.Engine->GetPlatform();
    const auto metadata = session.GetSaveDirectory() / "checkpoint.current";
    const auto before = Read(metadata);
    Require(session.Execute(pixel::NativeCaller::Driver,
                pixel::api::ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}}).Applied(),
            "default process close fixture could not launch");
    const auto stamp = session.CurrentCommit();
    pixel::testing::SessionAccess::RestoreClock(session, 0, 1.0 / 60, true);
    game.OnKeyEvent(Key::KEY_F2, 0, ButtonAction::ACTION_PRESS, Mod{});
    for (int request = 0; request < 2; ++request) {
        if (request == 0) app.RequestClose();
        else glfwSetWindowShouldClose(platform->GetWindows(), GLFW_TRUE);
        Require(!app.ProcessCloseRequest() && !app.ProcessCloseRequest()
                    && session.LastPersistenceResult().Status == pixel::api::ControlStatus::Deferred
                    && session.CurrentCommit() == stamp && Read(metadata) == before,
                "pending close must be consumed, declined and preserve the session/save");
        Draw(app);
        Require(ImGui::FindWindowByName("World save") && ImGui::FindWindowByName("World save")->Active,
                "declined close was invisible with the debug panel hidden");
    }
    session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    session.Tick(1.f / 60, {});
    Require(!world.GetEnvironment().IsWaitingForBudget() && session.CurrentCommit().Tick == 1
                && !app.ProcessCloseRequest() && Read(metadata) == before,
            "finishing the pending step retried a discarded close or save");

    const auto temporary = std::filesystem::path(metadata.string() + ".tmp");
    std::filesystem::create_directory(temporary);
    app.RequestClose();
    Require(!app.ProcessCloseRequest() && session.LastPersistenceResult().Status == pixel::api::ControlStatus::Failed
                && session.IsReady() && Read(metadata) == before, "save failure closed or destroyed the session");
    Draw(app);
    std::filesystem::remove(temporary);
    Require(!app.ProcessCloseRequest(), "I/O repair automatically retried closing");
    // Exercise the production input registration after both rejection paths.
    platform->OnKeyButtonCallback(GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
    Draw(app);
    Require(app.ProcessCloseRequest() && session.LastPersistenceResult().Status == pixel::api::ControlStatus::Saved,
            "explicit Escape retry did not save and accept closing");
    const auto saved = Read(metadata);
    Require(saved != before && saved.starts_with("AxiomPixelCheckpoint 1"), "accepted close did not persist the committed clock");
    Require(session.GetFormulaRuntime().Projectiles().size() == 1, "close lost the active default bolt");
    const auto running = session.GetFormulaRuntime().CaptureState();
    // A second save from GameEnd would fail here. Teardown must only clean up.
    std::filesystem::create_directory(temporary);
    app.Shutdown();
    Require(Read(metadata) == saved, "GameEnd rewrote the accepted save");
    std::filesystem::remove(temporary);
    pixel::GameSession restored(true);
    Require(restored.GetFormulaRuntime().CaptureState() == running,
            "accepted close did not restore the active default process exactly");
}

void TemporaryGameClosesWithoutSaving() {
    SaveRoot root;
    std::filesystem::create_directories(root.Path);
    std::ofstream(root.Path / "current.txt") << "unsupported old save\n";
    std::filesystem::path scratch;
    {
        PixelSmokeApplication app(std::make_unique<PixelGame>(), {true});
        app.Initialize();
        app.Begin();
        auto &session = static_cast<PixelGame &>(*app.GetGame()).GetSession();
        Require(!session.IsPersistent() && session.GetWorld().GetStreamer(),
                "default game is not a temporary streamed world");
        scratch = session.GetWorld().GetStreamer()->Settings().Directory;
        pixel::testing::SessionAccess::RestoreClock(session, 0, 1.0 / 60, true);
        app.RequestClose();
        Require(app.ProcessCloseRequest(), "temporary game tried to save a pending step on close");
        app.Shutdown();
    }
    Require(!std::filesystem::exists(scratch) && Read(root.Path / "current.txt") == "unsupported old save\n",
            "temporary close leaked its cache or changed the old save");
}
} // namespace

int main() {
    try {
        TemporaryGameClosesWithoutSaving();
        CloseAtSaveBoundary();
        std::cout << "Pixel close passed: pending/failed save refusal, visible notice, explicit retry with active default process, restore and teardown-only GameEnd\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
