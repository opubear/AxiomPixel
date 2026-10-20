#include "PixelSmokeApplication.h"
#include "Engine.h"
#include "HAL/Platform/PlatformInterface.h"
#include "InputManager.h"
#include "Application/PixelGame.h"
#include "Render/RenderGlobalContext.h"
#include "SystemEnum.h"
#include "DebugUI/ImguiManager.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
std::vector<pixel::CellState> CopyWorldCells(const pixel::World &world) {
    const auto state = world.ReadState();
    std::vector<pixel::CellState> result(state.CellCount());
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = state.AtIndex(i);
    return result;
}
void Draw(Axiom &app, int count = 1) {
    auto &engine = *app.Engine;
    for (int i = 0; i < count; ++i) {
        bool drawn = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            engine.GetPlatform()->PollEvents();
            if (!app.FrameBegin()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            engine.GetInput()->Dispatch();
            app.Tick(0); // Keep materials fixed to detect click-through.
            app.FrameEnd();
            drawn = true;
            break;
        }
        Require(drawn, "no drawable debug UI frame");
    }
}
void MovePointer(Axiom &app, ImVec2 p) {
    auto &engine = *app.Engine;
    auto *platform = engine.GetPlatform();
    glfwSetCursorPos(platform->GetWindows(), p.x, p.y);
    platform->PollEvents();
    platform->OnCursorPosCallback(p.x, p.y);
}
void ClickButton(Axiom &app, ImVec2 p, int button, int mods = 0) {
    auto &engine = *app.Engine;
    MovePointer(app, p);
    engine.GetPlatform()->OnButtonCallback(button, GLFW_PRESS, mods);
    Draw(app, 2);
    engine.GetPlatform()->OnButtonCallback(button, GLFW_RELEASE, mods);
    Draw(app, 2);
}
void Click(Axiom &app, ImVec2 p, int mods = 0) {
    ClickButton(app, p, GLFW_MOUSE_BUTTON_LEFT, mods);
}
void SendKey(Axiom &app, int code, bool down, int mods = 0) {
    auto &engine = *app.Engine;
    engine.GetPlatform()->OnKeyButtonCallback(code, 0, down ? GLFW_PRESS : GLFW_RELEASE, mods);
    Draw(app, 2);
}
// Tab navigation gives the actual production item's rectangle. This avoids
// duplicating row counts as optional solver diagnostics and memory rows change.
ImVec2 ControlPoint(Axiom &app, const char *label, const char *owner = "Pixel Debug", const char *child = nullptr) {
    auto *window = ImGui::FindWindowByName(owner);
    if (window && child) {
        auto *root = window;
        window = nullptr;
        for (auto *candidate : ImGui::GetCurrentContext()->Windows)
            if (candidate->RootWindow == root && std::strstr(candidate->Name, child)) {
                window = candidate;
                break;
            }
    }
    Require(window && window->Active, "default Pixel Debug window is missing");
    const auto id = window->GetID(label);
    auto &context = *ImGui::GetCurrentContext();
    // The default Tab policy visits text inputs only. Enable full navigation
    // while locating controls, then restore production settings before clicking.
    const auto originalFlags = context.IO.ConfigFlags;
    context.IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const auto padding = ImGui::GetStyle().WindowPadding;
    Click(app, {window->Pos.x + padding.x / 2, window->Pos.y + window->TitleBarHeight + padding.y / 2});
    for (int attempt = 0; attempt < 128; ++attempt) {
        if (context.NavWindow == window && context.NavId == id && context.NavIdIsAlive) {
            const auto rect = ImGui::WindowRectRelToAbs(window, window->NavRectRel[ImGuiNavLayer_Main]);
            context.IO.ConfigFlags = originalFlags;
            return {rect.Min.x + std::min(30.f, rect.GetWidth() / 2), rect.Min.y + rect.GetHeight() / 2};
        }
        SendKey(app, GLFW_KEY_TAB, true);
        SendKey(app, GLFW_KEY_TAB, false);
    }
    context.IO.ConfigFlags = originalFlags;
    throw std::runtime_error(std::string("keyboard navigation did not locate production control: ") + label);
}
void SelectWindStrategy(Axiom &app, int selected) {
    Require(selected >= 0 && selected < 7, "invalid wind strategy fixture index");
    Click(app, ControlPoint(app, "Wind strategy"));
    const auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
    Require(!popups.empty() && popups.back().Window, "wind strategy combo did not open");
    const auto *popup = popups.back().Window;
    const auto &style = ImGui::GetStyle();
    const float text = ImGui::GetFontSize();
    Click(app, {popup->Pos.x + style.WindowPadding.x + 30,
                popup->Pos.y + style.WindowPadding.y + selected * (text + style.ItemSpacing.y) + text * .5f});
    Require(ImGui::GetCurrentContext()->OpenPopupStack.empty(), "strategy selection did not close the production combo");
}
void EnterBoundary(Axiom &app, const char *value) {
    auto &engine = *app.Engine;
    Click(app, ControlPoint(app, "Wind boundary Y"));
    const auto id = ImGui::FindWindowByName("Pixel Debug")->GetID("Wind boundary Y");
    Require(ImGui::GetActiveID() == id && ImGui::GetInputTextState(id),
            "click did not focus the production world-Y InputScalar");

    // InputScalar is a text input. Exercise real platform keyboard shortcuts
    // and character events, rather than the removed slider temporary-input API.
    const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
    const int modifier = mac ? GLFW_KEY_LEFT_SUPER : GLFW_KEY_LEFT_CONTROL;
    const int mask = mac ? GLFW_MOD_SUPER : GLFW_MOD_CONTROL;
    SendKey(app, modifier, true, mask);
    SendKey(app, GLFW_KEY_A, true, mask);
    SendKey(app, GLFW_KEY_A, false, mask);
    SendKey(app, modifier, false);
    for (const char *character = value; *character; ++character)
        engine.GetPlatform()->OnCharacterCallback(unsigned(*character));
    Draw(app, 2);
    SendKey(app, GLFW_KEY_ENTER, true);
    SendKey(app, GLFW_KEY_ENTER, false);
}

void CheckMemoryPanel(Axiom &app) {
    auto *window = ImGui::FindWindowByName("Pixel Debug");
    window->StateStorage.SetInt(window->GetID("Environment"), 0);
    window->StateStorage.SetInt(window->GetID("Wind appearance"), 0);
    window->StateStorage.SetInt(window->GetID("Performance"), 1);
    Draw(app, 2);

    // Use the production checkbox after whatever camera and diagnostics rows
    // are present. A real click must enable the frame-end collector.
    Click(app, ControlPoint(app, "Collect CPU timings and memory"));

    // The table appears only after actual frame-end memory has been collected
    // and the normal half-second display refresh has consumed it.
    for (int attempt = 0; attempt < 100; ++attempt) {
        Draw(app);
        const auto *table = ImGui::TableFindByID(window->GetID("Performance memory"));
        if (table && table->LastFrameActive == ImGui::GetFrameCount())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(false, "production memory table did not receive frame-end samples");
}
} // namespace

void RunPixelDebugUISmoke() {
    auto game = std::make_unique<PixelGame>(false);
    auto *pixel = game.get();
    pixel->SetControlMode(PixelGame::ControlMode::DebugCamera);
    PixelSmokeApplication app(std::move(game), {true});
    auto &engine = *app.Engine;
    app.Initialize();
    app.Begin();
    Draw(app, 3);
    auto *formulaWindow = ImGui::FindWindowByName("Formula Editor");
    Require(formulaWindow && formulaWindow->Active, "default Formula Editor is missing");
    Click(app, {formulaWindow->Pos.x + formulaWindow->Size.x - ImGui::GetStyle().FramePadding.x - ImGui::GetFontSize() / 2,
                formulaWindow->Pos.y + formulaWindow->TitleBarHeight / 2});
    Require(!ImGui::FindWindowByName("Formula Editor")->Active, "Formula editor close button did not hide the separate window");
    Require(app.GetDebugUI() && !engine.GetScene() && !g_render_context, "2D UI created a 3D scene/material context");
    Require(ImGui::GetDrawData()->TotalVtxCount > 0 && app.GetDebugUI()->FrameBuffers.LiveBufferCount() > 0,
            "debug UI did not upload GPU geometry");
    Require(app.GetDebugUI()->pushConstBlock.scale.X() > 0, "Pixel present pass did not record the UI overlay");
    auto &world = pixel->GetWorld();
    Require(world.GetWindStrategyKind() == pixel::WindStrategyKind::Empty && world.GetWindEndRow() == 424,
            "default UI must preserve empty wind and world Y = 424");
    SelectWindStrategy(app, int(pixel::WindStrategyKind::Coarse));
    const auto original = CopyWorldCells(world);
    const auto originalOrigin = pixel->GetView().OriginPixels;

    EnterBoundary(app, "512");
    Require(world.GetWindBoundary() == 512 && world.GetWindEndRow() == 512 && world.SampleWind(12.5, 511.5).X != 0 && world.SampleWind(12.5, 512.0).X == 0,
            "world-Y boundary 512 did not include exactly the rows above it");
    EnterBoundary(app, "0");
    Require(world.GetWindBoundary() == 0 && world.GetWindEndRow() == 0,
            "world-Y zero must leave the finite player window outside the wind");
    EnterBoundary(app, "-2147483649");
    Require(world.GetWindBoundary() == -2147483649LL && world.GetWindEndRow() == 0,
            "negative world-Y input was clamped or truncated to 32 bits");
    EnterBoundary(app, "2048");
    Require(world.GetWindBoundary() == 2048 && world.GetWindEndRow() == 1024 && world.SampleWind(12.5, 1023.5).X != 0,
            "boundary below the player window did not cover all its rows");
    EnterBoundary(app, "9007199254740993");
    Require(world.GetWindBoundary() == 9007199254740993LL && world.GetWindEndRow() == 1024,
            "large world-Y input lost integer precision or was clamped to the window");
    Click(app, ControlPoint(app, "Default boundary (Y = 424)"));
    Require(world.GetWindBoundary() == 424 && world.GetWindEndRow() == 424,
            "default boundary button did not restore world Y = 424");
    Require(original == CopyWorldCells(world),
            "editing UI placed/removed dirt or altered the base force field");
    Require(pixel->GetView().OriginPixels == originalOrigin, "typing in UI moved the gameplay view");

    // Exercise the production combo with real input on a one-row wind domain;
    // this avoids running the expensive reference while still checking switching.
    EnterBoundary(app, "1");
    world.SetBaseForce(2, 2, {123, 456});
    for (const int selected : {1, 2, 3, 4, 5, 6, 0}) {
        SelectWindStrategy(app, selected);
        Require(int(world.GetWindStrategyKind()) == selected && world.GetWindBoundary() == 1 && world.GetWindEndRow() == 1,
                "production combo selected the wrong wind strategy");
        Require(world.GetBaseForceField().Get(2, 2) == pixel::ForceVector{123, 456},
                "UI strategy switch changed independent force storage");
        Require(original == CopyWorldCells(world),
                "strategy combo input changed materials");
    }
    world.SetBaseForce(2, 2, {0, 3600});
    EnterBoundary(app, "424");

    // Unfocus by clicking outside the panel; world interaction must still work.
    auto *platform = engine.GetPlatform();
    {
        auto view = pixel->GetView();
        view.OriginPixels = {0, 0};
        view.WorldOriginCells = {};
        pixel->SetView(view);
    }
    const ImVec2 outside{600, 400};
    int ww, wh, fw, fh;
    glfwGetWindowSize(platform->GetWindows(), &ww, &wh);
    platform->GetFramebufferSize(fw, fh);
    const auto picked = PickGridCell(pixel->GetView(), {outside.x, outside.y}, {ww, wh}, {fw, fh});
    Require(picked.has_value(), "outside UI fixture missed world");
    Click(app, outside);
    Require(world.GetCell((*picked)[0], (*picked)[1]).Type == pixel::CellTypes::Dirt,
            "outside UI click did not reach gameplay");
    SendKey(app, GLFW_KEY_F1, true);
    SendKey(app, GLFW_KEY_F1, false);
    Require(pixel->GetPresentation().DisplayMode == GridDisplayMode::ForceField, "UI prevented F1 after losing focus");
    SendKey(app, GLFW_KEY_F2, true);
    SendKey(app, GLFW_KEY_F2, false);
    Require(!ImGui::FindWindowByName("Pixel Debug")->Active, "F2 did not hide debug UI");
    SendKey(app, GLFW_KEY_F2, true);
    SendKey(app, GLFW_KEY_F2, false);
    Require(ImGui::FindWindowByName("Pixel Debug")->Active, "F2 did not reopen debug UI");
    EnterBoundary(app, "0");
    Click(app, outside);
    SendKey(app, GLFW_KEY_R, true);
    SendKey(app, GLFW_KEY_R, false);
    Require(world.GetWindEndRow() == 424 && world.GetWindStrategyKind() == pixel::WindStrategyKind::Empty
                && world.GetCell(0, 511).Type == pixel::CellTypes::Empty
                && world.GetCell(0, 512).Type == pixel::CellTypes::Dirt,
            "R did not restore default wind and terrain");
    const auto oldGeneration = engine.GetRHI()->GetSwapchainGeneration();
    glfwSetWindowSize(platform->GetWindows(), 720, 540);
    // Window resize notifications are asynchronous; wait for the condition, not
    // an arbitrary number of fast frames that may all precede the notification.
    for (int attempt = 0; attempt < 100 && engine.GetRHI()->GetSwapchainGeneration() == oldGeneration; ++attempt) {
        Draw(app);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(engine.GetRHI()->GetSwapchainGeneration() > oldGeneration, "debug UI resize did not recreate the swapchain");
    EnterBoundary(app, "512");
    Require(world.GetWindEndRow() == 512, "UI stopped working after resize");
    CheckMemoryPanel(app);
    app.Shutdown();
    Require(!ImGui::GetCurrentContext() && !g_render_context, "debug UI context survived shutdown");
    std::cout << "Pixel ImGui Environment passed: seven strategies, int64 world-Y input/coverage, UI capture, F1/F2/reset, resize, memory collection/table, 2D-only lifecycle\n";
}

void RunPixelFormulaUISmoke() {
    auto game = std::make_unique<PixelGame>(false);
    auto *pixel = game.get();
    PixelSmokeApplication app(std::move(game), {true});
    app.Initialize();
    app.Begin();
    Draw(app, 3);
    auto &runtime = pixel->GetSession().GetFormulaRuntime();
    auto *editor = ImGui::FindWindowByName("Formula Editor");
    Require(editor && editor->Active, "separate Formula Editor did not open");
    SendKey(app, GLFW_KEY_F2, true);
    SendKey(app, GLFW_KEY_F2, false);
    Require(!ImGui::FindWindowByName("Pixel Debug")->Active && editor->Active,
            "F2 must hide only Pixel Debug while Formula Editor remains usable");
    const auto worldBefore = CopyWorldCells(pixel->GetWorld());
    ClickButton(app, {editor->Pos.x + 20, editor->Pos.y + 120}, GLFW_MOUSE_BUTTON_RIGHT);
    Require(runtime.Projectiles().empty() && CopyWorldCells(pixel->GetWorld()) == worldBefore,
            "right click inside Formula Editor leaked into gameplay");

    const auto choose = [&](const char *label, int row, const char *child = nullptr) {
        Click(app, ControlPoint(app, label, "Formula Editor", child));
        const auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
        Require(!popups.empty() && popups.back().Window, "Formula combo did not open");
        const auto *popup = popups.back().Window;
        const auto &style = ImGui::GetStyle();
        Click(app, {popup->Pos.x + style.WindowPadding.x + 35,
                    popup->Pos.y + style.WindowPadding.y + row * (ImGui::GetFontSize() + style.ItemSpacing.y) + ImGui::GetFontSize() / 2});
    };
    choose("Preset (draft only)", 1);
    Require(runtime.ActiveFormula().Kind() == pixel::formula::NodeKind::Bolt,
            "preset changed the active description before Apply");
    Click(app, ControlPoint(app, "Apply", "Formula Editor"));
    Require(runtime.ActiveFormula().Kind() == pixel::formula::NodeKind::Fan,
            "production Apply did not install fan preset");

    choose("New node type", int(pixel::formula::NodeKind::Sequence), "/Parameters_");
    Click(app, ControlPoint(app, "Wrap selected", "Formula Editor", "/Parameters_"));
    Click(app, ControlPoint(app, "Apply", "Formula Editor"));
    Require(runtime.ActiveFormula().Kind() == pixel::formula::NodeKind::Sequence
                && runtime.ActiveFormula().ChildCount() == 1
                && runtime.ActiveFormula().Child(0).Kind() == pixel::formula::NodeKind::Fan,
            "production tree wrap did not change the structure");

    SendKey(app, GLFW_KEY_F2, true);
    SendKey(app, GLFW_KEY_F2, false);
    Require(ImGui::FindWindowByName("Pixel Debug")->Active && editor->Active,
            "F2 must reopen only Pixel Debug");
    SendKey(app, GLFW_KEY_F4, true);
    SendKey(app, GLFW_KEY_F4, false);
    Require(!editor->Active && ImGui::FindWindowByName("Pixel Debug")->Active,
            "F4 must hide only Formula Editor");
    SendKey(app, GLFW_KEY_F2, true);
    SendKey(app, GLFW_KEY_F2, false);
    Require(!editor->Active && !ImGui::FindWindowByName("Pixel Debug")->Active,
            "F2 must not reopen a hidden Formula Editor");
    auto *platform = app.Engine->GetPlatform();
    int width = 0, height = 0;
    glfwGetWindowSize(platform->GetWindows(), &width, &height);
    ClickButton(app, {float(width / 2 + 140), float(height / 2)}, GLFW_MOUSE_BUTTON_RIGHT);
    Require(runtime.Projectiles().size() == 3, "real outside right click did not activate three-way fan");
    SendKey(app, GLFW_KEY_F4, true);
    SendKey(app, GLFW_KEY_F4, false);
    Require(editor->Active && !ImGui::FindWindowByName("Pixel Debug")->Active,
            "F4 must reopen Formula Editor while Pixel Debug stays hidden");
    Click(app, ControlPoint(app, "Delete", "Formula Editor", "/Parameters_"));
    Click(app, ControlPoint(app, "Apply", "Formula Editor"));
    Require(runtime.ActiveFormula().Kind() == pixel::formula::NodeKind::Empty && runtime.Projectiles().size() == 3,
            "deleting the root did not produce Empty or mutated old instances");
    Click(app, ControlPoint(app, "Clear running", "Formula Editor"));
    Require(runtime.Projectiles().empty() && runtime.Stats().Pending == 0, "Clear running retained active or delayed work");
    editor = ImGui::FindWindowByName("Formula Editor");
    editor->Collapsed = true;
    Draw(app, 2);
    Require(CopyWorldCells(pixel->GetWorld()) == worldBefore, "Formula UI structure editing changed world cells");
    app.Shutdown();
    std::cout << "Formula ImGui passed: preset/Apply, tree wrap/delete, version isolation, right capture, independent F2/F4 and clear\n";
}
