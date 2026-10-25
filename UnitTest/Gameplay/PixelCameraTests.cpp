#include "Runtime/Gameplay/Camera/Camera2D.h"
#include "Runtime/Render/PixelWorldPresentation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        pixel::Camera2D camera;
        camera.SetFollowRate(8);
        camera.Follow({100, -50}, 0.125);
        const double blend = 1 - std::exp(-1.0);
        Require(std::abs(camera.GetPosition()[0] - 100 * blend) < 1e-10, "distance-proportional follow");
        Require(std::abs(camera.GetPosition()[1] + 50 * blend) < 1e-10, "signed vertical follow");
        for (int fps : {30, 60, 144}) {
            pixel::Camera2D partitioned;
            for (int frame = 0; frame < fps; ++frame)
                partitioned.Follow({100, -50}, 1.0 / fps);
            Require(std::abs(partitioned.GetPosition()[0] - 100 * (1 - std::exp(-8.0))) < 1e-10,
                    "stationary target follow must be invariant under frame partition");
        }
        pixel::Camera2D near, far;
        near.Follow({1, 0}, 0.001);
        far.Follow({10, 0}, 0.001);
        Require(std::abs(far.GetPosition()[0] - near.GetPosition()[0] * 10) < 1e-12, "speed must scale with distance");
        double previous = camera.GetPosition()[0];
        for (int i = 0; i < 100; ++i) {
            camera.Follow({100, -50}, 1.0 / 60);
            Require(camera.GetPosition()[0] >= previous && camera.GetPosition()[0] <= 100, "follow must not overshoot or oscillate");
            previous = camera.GetPosition()[0];
        }
        camera.Follow({-200, 100}, 100);
        Require(camera.GetPosition() == pixel::Camera2D::Position{-200, 100}, "large delta must remain bounded by target");
        camera.SetFollowRate(0);
        camera.Follow({0, 0}, 1);
        camera.Follow({0, 0}, NAN);
        camera.Follow({0, 0}, -1);
        Require(camera.GetPosition() == pixel::Camera2D::Position{-200, 100}, "zero rate and invalid delta leave position unchanged");
        bool rejected = false;
        try { camera.SetFollowRate(-1); } catch (const std::invalid_argument &) { rejected = true; }
        Require(rejected, "negative rate must be rejected");
        camera.SetPosition({512, 510});
        GridView2D settings;
        const auto view = MakeCameraView(camera, {1600, 1200}, settings);
        Require(view.OriginPixels == std::array<int32_t, 2>{7392, 7560}, "world centre must determine visible origin");
        const auto resized = MakeCameraView(camera, {1280, 960}, settings);
        Require(resized.OriginPixels == std::array<int32_t, 2>{7552, 7680}, "resize must retain world centre");
        settings.CellSizePixels = {32, 32};
        const auto zoomed = MakeCameraView(camera, {1600, 1200}, settings);
        Require(zoomed.OriginPixels == std::array<int32_t, 2>{15584, 15720}, "zoom must retain world centre");
        const auto picked = PickGridCell(zoomed, {400, 300}, {800, 600}, {1600, 1200});
        Require(picked && *picked == std::array<int64_t, 2>{512, 510}, "HiDPI picking must agree with camera view");
        camera.Translate({-0.25, 0.125});
        Require(view.OriginPixels == std::array<int32_t, 2>{7392, 7560}, "published view must not track live camera");
        Require(camera.GetPosition() == pixel::Camera2D::Position{511.75, 510.125}, "camera must retain fractional movement");
        std::cout << "Camera smoothing and view projection tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
