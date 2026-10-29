#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Render/PixelWorldPresentation.h"
#include "Runtime/Render/FormulaFrame.h"
#include "Runtime/Render/DeviceFrame.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template <class F> void Reject(F action) {
    bool rejected = false;
    try { action(); } catch (const std::invalid_argument &) { rejected = true; }
    Check(rejected, "invalid logical pixel shape was accepted");
}

void ProjectionAndPicking() {
    pixel::GameCamera camera;
    camera.SetFramebufferSize({1600, 1200});
    Check(camera.GetView().CellSizePixels == pixel::DefaultLogicalPixelSize, "default camera scale differs from configuration");
    const pixel::WorldPosition centre{{-(int64_t{1} << 40), int64_t{1} << 40}, {32, 32}};
    const auto reference = centre.Cell();
    for (uint32_t scale : {1, 2, 4, 8, 16, 32, 64}) {
        GridView2D settings;
        settings.CellSizePixels = {scale, scale};
        camera.SetView(settings);
        camera.GetCamera().SetWorldPosition(centre);
        for (int dpi : {1, 2}) {
            const std::array<int, 2> framebuffer{800 * dpi, 600 * dpi};
            const auto view = camera.GetViewForFramebuffer(framebuffer);
            // Every framebuffer sample in one logical cell must pick that cell,
            // including its final row/column at both normal and Retina density.
            for (uint32_t y = 0; y < scale; ++y)
                for (uint32_t x = 0; x < scale; ++x) {
                    const MousePointerState pointer{{400 + (scale + x + .5) / dpi,
                                                      300 + (scale + y + .5) / dpi}, {800, 600}, framebuffer};
                    const auto picked = camera.PickCell(pointer);
                    Check(picked == std::array<int64_t, 2>{reference.X + 1, reference.Y + 1}, "square pixel block picked the wrong logical cell");
                    const auto aim = camera.PickWorldPosition(pointer);
                    Check(aim && aim->Cell() == pixel::CellCoord{reference.X + 1, reference.Y + 1}, "continuous aim uses another scale");
                }
            const auto visible = VisibleWorldRegion(view, framebuffer);
            Check(visible.X <= reference.X - framebuffer[0] / (2 * scale) &&
                  visible.X + visible.Width > reference.X + framebuffer[0] / (2 * scale), "visible region did not expand in logical units");
        }

        WorldGridFrame frame;
        frame.View = camera.GetView();
        const pixel::ActorSnapshot actor{{1}, pixel::ActorKind::Player, centre, true};
        BuildCharacterFrame(actor, camera.GetCamera(), {1600, 1200}, frame);
        Check(frame.Character->Bounds == std::array<float, 4>{800, 600, float(pixel::Body::Width * scale), float(pixel::Body::Height * scale)}, "body dimensions are not measured in logical pixels");
        pixel::FormulaProjectile projectile;
        projectile.Position = centre;
        projectile.Radius = .5;
        BuildFormulaFrame(std::span(&projectile, 1), camera.GetCamera(), {1600, 1200}, frame);
        Check(frame.FormulaDraws.size() == 1 && frame.FormulaDraws[0].CenterRadius == std::array<float, 4>{800, 600, scale * .5f, scale * .5f}, "projectile changed logical radius or lost square projection");
        pixel::DeviceSnapshot device;
        device.X = reference.X;
        device.Y = reference.Y;
        BuildDeviceFrame(std::span(&device, 1), camera.GetCamera(), {1600, 1200}, frame);
        Check(frame.DeviceDraws.size() == 1 && frame.DeviceDraws[0].CenterRadius == std::array<float, 4>{800.f + scale, 600.f + scale, float(scale), float(scale)}, "device dimensions use another scale");

        camera.Zoom(true);
        Check(camera.GetCamera().GetWorldPosition().Cell() == reference &&
              pixel::IsValidLogicalPixelSize(camera.GetView().CellSizePixels), "zoom changed world position or pixel shape");
    }
}

void InvalidShapes() {
    pixel::GameCamera camera;
    camera.SetFramebufferSize({800, 600});
    const auto before = camera.GetView();
    for (const auto size : {std::array<uint32_t, 2>{0, 0}, {4, 8}, {65, 65}, {UINT32_MAX, UINT32_MAX}}) {
        auto view = before;
        view.CellSizePixels = size;
        Reject([&] { camera.SetView(view); });
        Check(camera.GetView().CellSizePixels == before.CellSizePixels && camera.GetView().OriginPixels == before.OriginPixels,
              "invalid scale partially changed camera state");
        Reject([&] { MakeCameraView(camera.GetCamera(), {800, 600}, view); });
        Reject([&] { VisibleWorldRegion(view, {800, 600}); });
        Check(!PickGridCell(view, {0, 0}, {800, 600}, {800, 600}), "invalid view accepted a click");
        WorldGridFrame frame;
        frame.View = view;
        Reject([&] { ValidateWorldGridFrame(frame.Read(), 1, 800, 600); });
    }
}
} // namespace

int main() {
    try {
        ProjectionAndPicking();
        InvalidShapes();
        std::cout << "Logical and render pixel separation passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
