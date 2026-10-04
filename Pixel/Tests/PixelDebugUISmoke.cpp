#include "Axiom.h"
#include "Engine.h"
#include "HAL/Platform/PlatformInterface.h"
#include "HAL/RenderHardwareInterface/RHIFactory.h"
#include "InputManager.h"
#include "PixelGame.h"
#include "Render/RenderGlobalContext.h"
#include "SystemEnum.h"
#include "UI/Imgui/ImguiManager.h"
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
void Draw(GameEngine &engine, int count = 1) {
    for (int i = 0; i < count; ++i) {
        bool drawn = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            engine.GetPlatform()->PollEvents();
            auto frame = engine.FrameBegin();
            if (!frame.Ready) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            engine.GetInput()->Dispatch();
            engine.Tick(0, frame.Token); // Keep materials fixed to detect click-through.
            engine.FrameEnd(frame.Token);
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
void MovePointer(GameEngine &engine, ImVec2 p) {
    auto *platform = engine.GetPlatform();
    glfwSetCursorPos(platform->GetWindows(), p.x, p.y);
    platform->PollEvents();
    platform->OnCursorPosCallback(p.x, p.y);
}
void Click(GameEngine &engine, ImVec2 p, int mods = 0) {
    MovePointer(engine, p);
    engine.GetPlatform()->OnButtonCallback(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, mods);
    Draw(engine, 2);
    engine.GetPlatform()->OnButtonCallback(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, mods);
    Draw(engine, 2);
}
void SendKey(GameEngine &engine, int code, bool down, int mods = 0) {
    engine.GetPlatform()->OnKeyButtonCallback(code, 0, down ? GLFW_PRESS : GLFW_RELEASE, mods);
    Draw(engine, 2);
}
void EnterHeight(GameEngine &engine, const char *value) {
    // ImGui maps Command to Ctrl for shortcuts on macOS, including slider edits.
    const bool mac = ImGui::GetIO().ConfigMacOSXBehaviors;
    const int modifier = mac ? GLFW_KEY_LEFT_SUPER : GLFW_KEY_LEFT_CONTROL;
    const int mask = mac ? GLFW_MOD_SUPER : GLFW_MOD_CONTROL;
    SendKey(engine, modifier, true, mask);
    Click(engine, SliderPoint(0.5f), mask);
    SendKey(engine, modifier, false);
    Require(ImGui::TempInputIsActive(ImGui::FindWindowByName("Pixel Debug")->GetID("Wind start height")),
            "modifier+click did not activate the actual height input");
    // Select-all uses Command on macOS and Control on other platforms.
    SendKey(engine, modifier, true, mask);
    SendKey(engine, GLFW_KEY_A, true, mask);
    SendKey(engine, GLFW_KEY_A, false, mask);
    SendKey(engine, modifier, false);
    for (const char *p = value; *p; ++p)
        engine.GetPlatform()->OnCharacterCallback(unsigned(*p));
    Draw(engine, 2);
    SendKey(engine, GLFW_KEY_ENTER, true);
    SendKey(engine, GLFW_KEY_ENTER, false);
}
} // namespace

void RunPixelDebugUISmoke() {
    Axiom app;
    app.Config = GameConfig::Load();
    auto game = std::make_unique<PixelGame>();
    auto *pixel = game.get();
    auto setup = game->CreateRenderSetup(); // Same composition as the real executable.
    app.Engine = std::make_unique<GameEngine>(std::move(game), CreateRHI(), std::move(setup));
    auto &engine = *app.Engine;
    engine.EngineInit();
    engine.Begin();
    Draw(engine, 3);
    Require(engine.GetImgui() && !engine.GetScene() && !g_render_context, "2D UI created a 3D scene/material context");
    Require(ImGui::GetDrawData()->TotalVtxCount > 0 && engine.GetImgui()->FrameBuffers.LiveBufferCount() > 0,
            "debug UI did not upload GPU geometry");
    Require(engine.GetImgui()->pushConstBlock.scale.X() > 0, "Pixel present pass did not record the UI overlay");
    auto &world = pixel->GetWorld();
    Require(world.GetWind().GetEndRow() == 424, "UI changed the default 600 height before interaction");
    const auto state = world.ReadState();
    const std::vector<pixel::CellState> original(state.Cells.begin(), state.Cells.end());
    const auto originalOrigin = pixel->View.OriginPixels;

    Click(engine, SliderPoint(0.25f));
    Require(world.GetWind().GetEndRow() > 512 && world.GetWind().GetEndRow() < 900,
            "slider did not lower the wind boundary into terrain");
    EnterHeight(engine, "512");
    Require(world.GetWind().GetEndRow() == 512 && world.GetWind().Sample(12, 511.5f).X != 0
                && world.GetWind().Sample(12, 512).X == 0,
            "height 512 is not the bottom-origin boundary");
    EnterHeight(engine, "0");
    Require(world.GetWind().GetEndRow() == 1024 && world.GetWind().Sample(12, 1023.5f).X != 0,
            "height zero did not include the bottom row");
    EnterHeight(engine, "1024");
    Require(!world.GetWind().IsEnabled(), "height 1024 did not disable wind");
    EnterHeight(engine, "-20");
    Require(world.GetWind().GetEndRow() == 1024, "negative UI height was not clamped to zero");
    EnterHeight(engine, "2048");
    Require(!world.GetWind().IsEnabled(), "oversized UI height was not clamped to wind-off");
    EnterHeight(engine, "600");
    Require(world.GetWind().GetEndRow() == 424, "wind did not recover after disabling");
    Require(std::equal(original.begin(), original.end(), world.ReadState().Cells.begin()),
            "editing UI placed/removed dirt or altered the base force field");
    Require(pixel->View.OriginPixels == originalOrigin, "typing in UI moved the gameplay view");

    // Unfocus by clicking outside the panel; world interaction must still work.
    auto *platform = engine.GetPlatform();
    pixel->View.OriginPixels = {0, 0};
    const ImVec2 outside{600, 400};
    int ww, wh, fw, fh;
    glfwGetWindowSize(platform->GetWindows(), &ww, &wh);
    platform->GetFramebufferSize(fw, fh);
    const auto picked = PickGridCell(pixel->View, {outside.x, outside.y}, {ww, wh}, {fw, fh});
    Require(picked.has_value(), "outside UI fixture missed world");
    Click(engine, outside);
    Require(world.GetCell((*picked)[0], (*picked)[1]).Type == pixel::CellTypes::Dirt,
            "outside UI click did not reach gameplay");
    SendKey(engine, GLFW_KEY_F1, true);
    SendKey(engine, GLFW_KEY_F1, false);
    Require(pixel->DisplayMode == GridDisplayMode::ForceField, "UI prevented F1 after losing focus");
    SendKey(engine, GLFW_KEY_F2, true);
    SendKey(engine, GLFW_KEY_F2, false);
    Require(!ImGui::FindWindowByName("Pixel Debug")->Active, "F2 did not hide debug UI");
    SendKey(engine, GLFW_KEY_F2, true);
    SendKey(engine, GLFW_KEY_F2, false);
    Require(ImGui::FindWindowByName("Pixel Debug")->Active, "F2 did not reopen debug UI");
    EnterHeight(engine, "0");
    Click(engine, outside);
    SendKey(engine, GLFW_KEY_R, true);
    SendKey(engine, GLFW_KEY_R, false);
    Require(world.GetWind().GetEndRow() == 424 && world.GetCell(0, 511).Type == pixel::CellTypes::Empty
                && world.GetCell(0, 512).Type == pixel::CellTypes::Dirt,
            "R did not restore default wind and terrain");
    const auto oldGeneration = engine.GetRHI()->GetSwapchainGeneration();
    glfwSetWindowSize(platform->GetWindows(), 720, 540);
    Draw(engine, 8);
    Require(engine.GetRHI()->GetSwapchainGeneration() > oldGeneration, "debug UI resize did not recreate the swapchain");
    EnterHeight(engine, "512");
    Require(world.GetWind().GetEndRow() == 512, "UI stopped working after resize");
    engine.End();
    Require(!ImGui::GetCurrentContext() && !g_render_context, "debug UI context survived shutdown");
    app.Engine.reset();
    std::cout << "Pixel ImGui Environment passed: live height, bounds, UI capture, F1/F2/reset, resize, 2D-only lifecycle\n";
}
