#include "Runtime/Gameplay/Application/DisplaySettings.h"
#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Gameplay/UI/InventoryUI.h"
#include "Runtime/Render/PixelWorldPresentation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void Reject(F action) {
    bool rejected = false;
    try { action(); } catch (const std::exception &) { rejected = true; }
    Require(rejected, "invalid display scale was accepted");
}

void Config() {
    const auto defaults = pixel::DisplaySettings::Parse("{}");
    Require(defaults.SceneScale == 1 && defaults.UIScale == 1, "missing scales changed defaults");
    const auto settings = pixel::DisplaySettings::Parse(R"({"SceneScale":1.375,"UIScale":0.75,"Window":{}})");
    Require(settings.SceneScale == 1.375f && settings.UIScale == .75f, "fractional configuration lost precision");
    for (const auto invalid : {R"({"SceneScale":0})", R"({"UIScale":-1})", R"({"SceneScale":true})",
                               R"({"UIScale":"2"})", R"({"UIScale":null})", R"({"SceneScale":1000})",
                               R"({"UIScale":5})", R"([])"})
        Reject([&] { pixel::DisplaySettings::Parse(invalid); });
}

void SceneAndPicking() {
    pixel::GameCamera camera;
    camera.SetFramebufferSize({800, 600});
    const auto position = pixel::WorldPosition::FromChunkCells({-(int64_t{1} << 40), int64_t{1} << 40}, {32, 32});
    camera.GetCamera().SetWorldPosition(position);
    for (float scale : {.5f, .825f, 1.f, 1.375f, 2.f}) {
        camera.SetSceneScale(scale);
        Require(std::abs(camera.GetSceneScale() - scale) < 1e-6, "scene scale was quantized");
        Require(camera.GetCamera().GetWorldPosition().Chunk == position.Chunk && camera.GetCamera().GetWorldPosition().Local == position.Local, "scene scale moved the camera");
        for (int dpi : {1, 2}) {
            const std::array size{800 * dpi, 600 * dpi};
            const auto view = camera.GetViewForFramebuffer(size);
            const float pixels = view.CellSizePixels[0];
            const auto region = VisibleWorldRegion(view, size);
            WorldGridFrame frame;
            frame.View = view;
            frame.SetRegion({region.X, region.Y}, 1, 1);
            const auto origin = LocalGridPixelOrigin(frame.Read());
            for (int x = 0; x < size[0]; x += 13) {
                const MousePointerState pointer{{(x + .5) / dpi, 300.25}, {800, 600}, size};
                const auto picked = camera.PickCell(pointer);
                const auto localCell = int64_t(std::floor((origin[0] + x + .5) / pixels));
                Require(picked && (*picked)[0] == region.X + localCell, "rebased shader origin disagrees with picking");
            }
        }
    }
    camera.SetSceneScale(1.375f);
    camera.Reset({0, 0, 100, 100}, nullptr);
    Require(camera.GetSceneScale() == 1.375f, "world reset lost scene scale");
    for (float bad : {0.f, -1.f, INFINITY, std::numeric_limits<float>::quiet_NaN()}) {
        Reject([&] { camera.SetSceneScale(bad); });
        Require(camera.GetSceneScale() == 1.375f, "invalid input mutated camera");
    }
}

void IndependentUI() {
    pixel::GameCamera camera;
    pixel::ui::InventoryUI ui;
    const auto revision = ui.Paint().Revision;
    for (float scale : {.25f, .75f, 1.f, 1.5f, 4.f}) {
        ui.SetUIScale(scale);
        Require(camera.GetSceneScale() == 1, "UI scale changed scene");
        Require(ui.Paint().Revision == revision && ui.Paint().UIScale == scale, "scale repainted or failed to publish canvas");
        for (const auto size : {std::array{800, 600}, std::array{1600, 1200}, std::array{320, 240}}) {
            const auto placement = pixel::ui::InventoryLayout::Placement(size, scale);
            Require(placement.X + placement.Width <= size[0] && placement.Y + placement.Height <= size[1], "scaled UI escaped viewport");
            const auto slot = pixel::ui::InventoryLayout::QuickSlot(6);
            const float factor = placement.Width / pixel::ui::InventoryLayout::Width;
            Require(ui.PointerPress(placement.X + (slot.X + 24) * factor,
                                    placement.Y + (slot.Y + 24) * factor, size, true), "scaled UI leaked click");
            Require(ui.Read().Selected == 6, "scaled slot hit testing selected wrong slot");
            ui.SelectQuickSlot(0);
        }
    }
    camera.SetSceneScale(2);
    Require(ui.GetUIScale() == 4, "scene scale changed UI");
    for (float bad : {0.f, -1.f, 5.f, INFINITY, std::numeric_limits<float>::quiet_NaN()}) {
        Reject([&] { ui.SetUIScale(bad); });
        Require(ui.GetUIScale() == 4, "invalid UI input mutated scale");
    }
}
}

int main() {
    try {
        Config();
        SceneAndPicking();
        IndependentUI();
        std::cout << "Independent display scales, config, fractional projection and hit testing passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
