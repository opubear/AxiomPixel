#include "../Support/TestAccess.h"
#include "../Support/CheckpointFiles.h"
#include "PixelSmokeApplication.h"
#include "Asset/Loaders/AssetLoader.h"
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

void SendKey(Axiom &app, int key, int action) {
    app.Engine->GetPlatform()->OnKeyButtonCallback(key, 0, action, 0);
    Draw(app);
    Draw(app);
}

void PressEscape(Axiom &app) {
    SendKey(app, GLFW_KEY_ESCAPE, GLFW_PRESS);
    SendKey(app, GLFW_KEY_ESCAPE, GLFW_RELEASE);
}

void ClickMenuButton(Axiom &app, const char *label) {
    auto &menu = static_cast<PixelGame &>(*app.GetGame()).GetGameMenu();
    Require(menu.IsOpen(), "game menu was not open");
    auto *platform = app.Engine->GetPlatform();
    auto *window = platform->GetWindows();
    std::array<int, 2> framebuffer{}, size{};
    platform->GetFramebufferSize(framebuffer[0], framebuffer[1]);
    glfwGetWindowSize(window, &size[0], &size[1]);
    const auto bounds = pixel::ui::GameMenuLayout::Placement(framebuffer, menu.GetUIScale());
    const auto button = std::string_view(label) == "Quit game" ? pixel::ui::GameMenuLayout::QuitButton()
                                                               : pixel::ui::GameMenuLayout::ReturnButton();
    const ImVec2 point{(bounds.X + (button.X + button.Width / 2) * bounds.Width / pixel::ui::GameMenuLayout::Width) * size[0] / framebuffer[0],
                       (bounds.Y + (button.Y + button.Height / 2) * bounds.Height / pixel::ui::GameMenuLayout::Height) * size[1] / framebuffer[1]};
    glfwFocusWindow(platform->GetWindows());
    platform->PollEvents();
    glfwSetCursorPos(platform->GetWindows(), point.x, point.y);
    platform->OnCursorPosCallback(point.x, point.y);
    Draw(app);
    platform->OnButtonCallback(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
    Draw(app);
    platform->OnButtonCallback(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
    Draw(app);
    Draw(app);
}

void GameMenuInteraction(bool debugUI) {
    PixelSmokeApplication app(std::make_unique<PixelGame>(false), {debugUI});
    app.Initialize();
    app.Begin();
    auto &game = static_cast<PixelGame &>(*app.GetGame());
    game.OnKeyEvent(Key::KEY_F2, 0, ButtonAction::ACTION_PRESS, Mod{});
    game.OnKeyEvent(Key::KEY_F4, 0, ButtonAction::ACTION_PRESS, Mod{});
    Draw(app);
    SendKey(app, GLFW_KEY_ESCAPE, GLFW_PRESS);
    Require(game.IsGameMenuOpen() && !app.ProcessCloseRequest(), "Escape exited instead of opening the menu");
    SendKey(app, GLFW_KEY_ESCAPE, GLFW_REPEAT);
    Require(game.IsGameMenuOpen() && !app.ProcessCloseRequest(), "Escape repeat closed the menu");
    SendKey(app, GLFW_KEY_ESCAPE, GLFW_RELEASE);
    const auto stamp = game.GetSession().CurrentCommit();
    SendKey(app, GLFW_KEY_R, GLFW_PRESS);
    SendKey(app, GLFW_KEY_R, GLFW_RELEASE);
    Require(game.GetSession().CurrentCommit() == stamp, "menu allowed a world reset");
    PressEscape(app);
    Require(!game.IsGameMenuOpen() && !app.ProcessCloseRequest(), "second Escape failed to close only the menu");
    PressEscape(app);
    ClickMenuButton(app, "Return to game");
    Require(!game.IsGameMenuOpen() && !app.ProcessCloseRequest(), "return button did not close only the menu");
    PressEscape(app);
    ClickMenuButton(app, "Quit game");
    Require(app.ProcessCloseRequest(), "quit button did not request normal closing");
    Require(!game.GetSession().LastAction(pixel::NativeCaller::Input), "menu click leaked into a world action");
    app.Shutdown();
}

void GameMenuEditingCapture() {
    PixelSmokeApplication app(std::make_unique<PixelGame>(false), {true});
    app.Initialize();
    app.Begin();
    auto &game = static_cast<PixelGame &>(*app.GetGame());
    glfwFocusWindow(app.Engine->GetPlatform()->GetWindows());
    Draw(app);
    Draw(app);
    auto *editor = ImGui::FindWindowByName("Formula Editor");
    auto *debug = ImGui::FindWindowByName("Pixel Debug");
    Require(editor && editor->Active && debug && debug->Active, "default debug panels were not visible");

    // Keep the production text field visible and activate the real widget.
    // Input and frame callbacks remain those installed by PixelGame.
    editor->StateStorage.SetInt(editor->GetID("Formula presets"), 0);
    editor->StateStorage.SetInt(editor->GetID("Process source"), 1);
    ImGui::FocusWindow(editor);
    const auto textId = editor->GetID("Process text");
    ImGui::ActivateItemByID(textId);
    Draw(app);
    Draw(app);
    Require(ImGui::GetActiveID() == textId && ImGui::GetInputTextState(textId)
                && ImGui::GetIO().WantCaptureKeyboard,
            "production formula editor did not capture keyboard input");

    SendKey(app, GLFW_KEY_ESCAPE, GLFW_PRESS);
    Require(!game.IsGameMenuOpen() && ImGui::GetActiveID() != textId && !app.ProcessCloseRequest(),
            "first Escape failed to cancel editing without opening the menu");
    SendKey(app, GLFW_KEY_ESCAPE, GLFW_RELEASE);
    PressEscape(app);
    Require(game.IsGameMenuOpen() && !editor->Active && !debug->Active && !app.ProcessCloseRequest(),
            "Escape did not open the menu and temporarily hide the debug panels after editing");
    PressEscape(app);
    Require(!game.IsGameMenuOpen() && editor->Active && debug->Active && !app.ProcessCloseRequest(),
            "closing the menu did not restore the visible debug panels");
    app.Shutdown();
}

void CloseAtSaveBoundary() {
    SaveRoot root;
    PixelSmokeApplication app(std::make_unique<PixelGame>(true), {true});
    app.Initialize();
    app.Begin();
    auto &game = static_cast<PixelGame &>(*app.GetGame());
    auto &session = game.GetSession();
    // This save fixture launches along a fixed path; natural terrain can block
    // it before closing is exercised. Match the original flat-world fixture.
    Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SetNextGenerator{pixel::api::GeneratorKind::Flat}).Succeeded()
        && session.Control(pixel::NativeCaller::Driver, pixel::api::ResetWorld{}).Succeeded(), "close fixture reset failed");
    auto &world = pixel::testing::SessionAccess::World(session);
    auto *platform = app.Engine->GetPlatform();
    const auto metadata = session.GetSaveDirectory() / "checkpoint.current";
    const auto before = Read(metadata);
    const auto launch = session.Execute(pixel::NativeCaller::Driver,
                pixel::api::ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}});
    Require(launch.Queued(), "default process close fixture was not queued");
    app.RequestClose();
    Require(!app.ProcessCloseRequest() && session.LastPersistenceResult().Status == pixel::api::ControlStatus::Deferred,
            "close saved before queued changes reached simulation");
    session.Tick(1.f / 60, {});
    const auto completedLaunch = session.FindAction(launch.Key);
    if (!completedLaunch || !completedLaunch->Applied())
        throw std::runtime_error("default process close fixture could not launch: "
            + (completedLaunch ? completedLaunch->Message : "missing receipt"));
    const auto stamp = session.CurrentCommit();
    pixel::testing::SessionAccess::RestoreClock(session, session.GetSimulationTime(), 1.0 / 60, true);
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
    Require(!world.GetEnvironment().IsWaitingForBudget() && session.CurrentCommit().Tick == stamp.Tick + 1
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
    ImGui::ClearActiveID();
    ImGui::FocusWindow(nullptr);
    PressEscape(app);
    Require(game.IsGameMenuOpen() && !app.ProcessCloseRequest(), "Escape retried closing before choosing quit");
    ClickMenuButton(app, "Quit game");
    Require(app.ProcessCloseRequest() && session.LastPersistenceResult().Status == pixel::api::ControlStatus::Saved,
            "explicit menu quit retry did not save and accept closing");
    const auto saved = Read(metadata);
    Require(saved != before && saved.starts_with("AxiomPixelCheckpoint 1"), "accepted close did not persist the committed clock");
    Require(session.GetFormulaRuntime().Projectiles().size() == 1, "close lost the active default bolt");
    const auto running = session.GetFormulaRuntime().CaptureState();
    // A second save from GameEnd would fail here. Teardown must only clean up.
    std::filesystem::create_directory(temporary);
    app.Shutdown();
    Require(Read(metadata) == saved, "GameEnd rewrote the accepted save");
    std::filesystem::remove(temporary);
    pixel::GameSession restored(pixel::SessionMode::Persistent, {},
        pixel::MonsterCatalog::Parse(AssetLoader::LoadContentFile("Monsters.json")));
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

int main(int argc, char **argv) {
    try {
        GameMenuInteraction(false);
        GameMenuInteraction(true);
        GameMenuEditingCapture();
        if (argc > 1 && std::string_view(argv[1]) == "--menu-only") {
            std::cout << "Native game menu passed with and without ImGui: ESC, return, quit, active text capture and panel restoration\n";
            return 0;
        }
        TemporaryGameClosesWithoutSaving();
        CloseAtSaveBoundary();
        std::cout << "Pixel close passed: Escape menu/return/quit, active text capture, panel restoration, pending/failed save refusal, visible notice, explicit menu retry with active default process, restore and teardown-only GameEnd\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
