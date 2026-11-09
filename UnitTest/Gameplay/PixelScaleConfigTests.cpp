#include "Runtime/Gameplay/Camera/GameCamera.h"
#include <iostream>
#include <stdexcept>

// This executable is compiled with each supported example macro value and its
// own camera implementation, so defaults cannot silently use another build's macro.
int main() {
    try {
        constexpr uint32_t scale = EXPECTED_PIXEL_SCALE;
        const std::array<float, 2> size{float(scale), float(scale)};
        pixel::GameCamera camera;
        camera.SetFramebufferSize({800, 600});
        if (GridView2D{}.CellSizePixels != size || camera.GetView().CellSizePixels != size)
            throw std::runtime_error("configured scale did not reach the default view and camera");

        camera.SetSceneScale(1);
        camera.Reset({0, 0, 100, 100}, nullptr);
        const auto view = camera.GetView();
        if (view.CellSizePixels != size || camera.GetCamera().GetWorldPosition().RelativeCellsTo({}) != std::array<double, 2>{50, 50})
            throw std::runtime_error("reset changed logical coordinates or failed to restore configured scale");
        const MousePointerState pointer{{400 + scale + .25, 300 + scale + .25}, {800, 600}, {800, 600}};
        if (camera.PickCell(pointer) != std::array<int64_t, 2>{51, 51})
            throw std::runtime_error("configured projection and picking disagree");
        std::cout << "Configured logical pixel: " << scale << 'x' << scale << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
