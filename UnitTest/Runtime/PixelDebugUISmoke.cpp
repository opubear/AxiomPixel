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
#include <iostream>
#include <thread>

namespace {
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
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
// Locate the production slider from its window and ImGui layout metrics.
ImVec2 SliderPoint(float fraction) {
    const auto *window = ImGui::FindWindowByName("Pixel Debug");
    Require(window && window->Active, "default Pixel Debug window is missing");
    const auto &style = ImGui::GetStyle();
    const float text = ImGui::GetFontSize();
    const float frame = text + 2 * style.FramePadding.y;
    return {window->Pos.x + style.WindowPadding.x + 190 * fraction,
            window->Pos.y + window->TitleBarHeight + style.WindowPadding.y
                + frame + style.ItemSpacing.y + 2 * (text + style.ItemSpacing.y) + frame / 2};
}
void MovePointer(Axiom &app, ImVec2 p) {
    auto &engine = *app.Engine;
    auto *platform = engine.GetPlatform();
    glfwSetCursorPos(platform->GetWindows(), p.x, p.y);
    platform->PollEvents();
    platform->OnCursorPosCallback(p.x, p.y);
}
void Click(Axiom &app, ImVec2 p, int mods = 0) {
    auto &engine = *app.Engine;
    MovePointer(app, p);
    engine.GetPlatform()->OnButtonCallback(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, mods);
    Draw(app, 2);
    engine.GetPlatform()->OnButtonCallback(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, mods);
    Draw(app, 2);
}
void SelectWindStrategy(Axiom &app, int selected) {
    const auto &style = ImGui::GetStyle();
    const float text = ImGui::GetFontSize();
    const float frame = text + 2 * style.FramePadding.y;
    auto point = SliderPoint(.4f);
    point.y += 2 * frame + 3 * text + 5 * style.ItemSpacing.y;
    Click(app, point);
    const auto &popups = ImGui::GetCurrentContext()->OpenPopupStack;
    Require(!popups.empty() && popups.back().Window, "wind strategy combo did not open");
    const auto *popup = popups.back().Window;
    Click(app, {popup->Pos.x + style.WindowPadding.x + 30,
                   popup->Pos.y + style.WindowPadding.y + selected * (text + style.ItemSpacing.y) + text * .5f});
}
void SendKey(Axiom &app, int code, bool down, int mods = 0) {
    auto &engine = *app.Engine;
    engine.GetPlatform()->OnKeyButtonCallback(code, 0, down ? GLFW_PRESS : GLFW_RELEASE, mods);
    Draw(app, 2);
}
void EnterHeight(Axiom &app, const char *value) {
    auto &engine = *app.Engine;
    // ImGui maps Command to Ctrl for shortcuts on macOS, including slider edits.
    const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
    const int modifier = mac ? GLFW_KEY_LEFT_SUPER : GLFW_KEY_LEFT_CONTROL;
    const int mask = mac ? GLFW_MOD_SUPER : GLFW_MOD_CONTROL;
    SendKey(app, modifier, true, mask);
    Click(app, SliderPoint(0.5f), mask);
    SendKey(app, modifier, false);
    Require(ImGui::TempInputIsActive(ImGui::FindWindowByName("Pixel Debug")->GetID("Wind start height")),
            "modifier+click did not activate the actual height input");
    // Select-all uses Command on macOS and Control on other platforms.
    SendKey(app, modifier, true, mask);
    SendKey(app, GLFW_KEY_A, true, mask);
    SendKey(app, GLFW_KEY_A, false, mask);
    SendKey(app, modifier, false);
    for (const char *p = value; *p; ++p)
        engine.GetPlatform()->OnCharacterCallback(unsigned(*p));
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

    // Library-created games start with collection off. Click the production
    // checkbox after the collapsed sections and the four camera-control rows.
    const auto &style = ImGui::GetStyle();
    const float text = ImGui::GetFontSize();
    const float frame = text + 2 * style.FramePadding.y;
    Click(app, {window->Pos.x + style.WindowPadding.x + frame / 2,
                   window->Pos.y + window->TitleBarHeight + style.WindowPadding.y
                       + 5 * frame + 2 * text + 7 * style.ItemSpacing.y + frame / 2});

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
    auto game = std::make_unique<PixelGame>();
    auto *pixel = game.get();
    pixel->SetControlMode(PixelGame::ControlMode::DebugCamera);
    PixelSmokeApplication app(std::move(game), {true});
    auto &engine = *app.Engine;
    app.Initialize();
    app.Begin();
    Draw(app, 3);
    Require(app.GetDebugUI() && !engine.GetScene() && !g_render_context, "2D UI created a 3D scene/material context");
    Require(ImGui::GetDrawData()->TotalVtxCount > 0 && app.GetDebugUI()->FrameBuffers.LiveBufferCount() > 0,
            "debug UI did not upload GPU geometry");
    Require(app.GetDebugUI()->pushConstBlock.scale.X() > 0, "Pixel present pass did not record the UI overlay");
    auto &world = pixel->GetWorld();
    Require(world.GetWindStrategyKind() == pixel::WindStrategyKind::Empty && world.GetWindEndRow() == 424,
            "default UI must preserve empty wind and height 600");
    SelectWindStrategy(app, int(pixel::WindStrategyKind::Coarse));
    const auto state = world.ReadState();
    const std::vector<pixel::CellState> original(state.Cells.begin(), state.Cells.end());
    const auto originalOrigin = pixel->GetView().OriginPixels;

    Click(app, SliderPoint(0.25f));
    Require(world.GetWind().GetEndRow() > 512 && world.GetWind().GetEndRow() < 900,
            "slider did not lower the wind boundary into terrain");
    EnterHeight(app, "512");
    Require(world.GetWind().GetEndRow() == 512 && world.GetWind().Sample(12, 511.5f).X != 0
                && world.GetWind().Sample(12, 512).X == 0,
            "height 512 is not the bottom-origin boundary");
    EnterHeight(app, "0");
    Require(world.GetWind().GetEndRow() == 1024 && world.GetWind().Sample(12, 1023.5f).X != 0,
            "height zero did not include the bottom row");
    EnterHeight(app, "1024");
    Require(!world.GetWind().IsEnabled(), "height 1024 did not disable wind");
    EnterHeight(app, "-20");
    Require(world.GetWind().GetEndRow() == 1024, "negative UI height was not clamped to zero");
    EnterHeight(app, "2048");
    Require(!world.GetWind().IsEnabled(), "oversized UI height was not clamped to wind-off");
    EnterHeight(app, "600");
    Require(world.GetWind().GetEndRow() == 424, "wind did not recover after disabling");
    Require(std::equal(original.begin(), original.end(), world.ReadState().Cells.begin()),
            "editing UI placed/removed dirt or altered the base force field");
    Require(pixel->GetView().OriginPixels == originalOrigin, "typing in UI moved the gameplay view");

    // Exercise the production combo with real input on a one-row wind domain;
    // this avoids running the expensive reference while still checking switching.
    EnterHeight(app, "1023");
    world.SetBaseForce(2, 2, {123, 456});
    for (const int selected : {1, 2, 3, 4, 5, 6, 0}) {
        SelectWindStrategy(app, selected);
        Require(int(world.GetWindStrategyKind()) == selected && world.GetWindEndRow() == 1,
                "production combo selected the wrong wind strategy");
        Require(world.GetBaseForceField().Get(2, 2) == pixel::ForceVector{123, 456},
                "UI strategy switch changed independent force storage");
        Require(std::equal(original.begin(), original.end(), world.ReadState().Cells.begin()),
                "strategy combo input changed materials");
    }
    world.SetBaseForce(2, 2, {0, 3600});
    EnterHeight(app, "600");

    // Unfocus by clicking outside the panel; world interaction must still work.
    auto *platform = engine.GetPlatform();
    {
        auto view = pixel->GetView();
        view.OriginPixels = {0, 0};
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
    EnterHeight(app, "0");
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
    EnterHeight(app, "512");
    Require(world.GetWindEndRow() == 512, "UI stopped working after resize");
    CheckMemoryPanel(app);
    app.Shutdown();
    Require(!ImGui::GetCurrentContext() && !g_render_context, "debug UI context survived shutdown");
    std::cout << "Pixel ImGui Environment passed: six strategy selections, live height, bounds, UI capture, F1/F2/reset, resize, memory collection/table, 2D-only lifecycle\n";
}
