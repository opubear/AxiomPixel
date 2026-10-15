#include "GameCamera.h"
#include "PixelRender/PixelWorldPresentation.h"
#include <algorithm>
#include <stdexcept>

namespace pixel {
GameCamera::GameCamera() {
    // Linear RGB; the presentation target encodes this as sky blue.
    ViewSettings.Background = {0.25f, 0.55f, 0.85f, 1};
}
GridView2D GameCamera::GetView() const { return GetViewForFramebuffer(FramebufferSize); }
GridView2D GameCamera::GetViewForFramebuffer(std::array<int, 2> size) const {
    return MakeCameraView(Camera, size, ViewSettings);
}
void GameCamera::SetView(const GridView2D &view) {
    for (auto size : view.CellSizePixels)
        if (size < 1 || size > 64)
            throw std::invalid_argument("cell size must be in [1, 64]");
    Camera.SetPosition({(double(view.OriginPixels[0]) + FramebufferSize[0] / 2.0) / view.CellSizePixels[0],
                        (double(view.OriginPixels[1]) + FramebufferSize[1] / 2.0) / view.CellSizePixels[1]});
    ViewSettings = view;
}
void GameCamera::Reset(const World &world) {
    ViewSettings.CellSizePixels = {16, 16};
    Camera.SetPosition({world.ReadState().Width / 2.0, world.ReadState().Height / 2.0});
    if (world.GetPlayer()) {
        const auto &player = *world.GetPlayer();
        Camera.SetPosition({player.X + Character::Width / 2, player.Y + Character::Height / 2});
    }
}
void GameCamera::FollowPlayer(const World &world, float duration) {
    if (!world.GetPlayer())
        return;
    const auto &player = *world.GetPlayer();
    Camera.Follow({player.X + Character::Width / 2, player.Y + Character::Height / 2}, std::min(duration, .25f));
}
void GameCamera::Pan(std::array<int, 2> direction, float duration) {
    // Keep 240 framebuffer pixels/second, independently of zoom.
    const float distance = std::min(duration, .25f) * 240;
    Camera.Translate({direction[0] * distance / ViewSettings.CellSizePixels[0],
                      direction[1] * distance / ViewSettings.CellSizePixels[1]});
}
void GameCamera::Zoom(bool in) {
    const uint32_t old = ViewSettings.CellSizePixels[0];
    const uint32_t next = in ? std::min(64u, old * 2) : std::max(1u, old / 2);
    ViewSettings.CellSizePixels = {next, next};
}
void GameCamera::NextRegion() {
    auto view = GetView();
    view.OriginPixels[1] = ((std::max(0, view.OriginPixels[1]) / int(view.CellSizePixels[1]) / int(Grid2D::PageHeight) + 1)
                           % int(Grid2D::PageCount)) * int(Grid2D::PageHeight * view.CellSizePixels[1]);
    SetView(view);
}
std::optional<std::array<uint32_t, 2>> GameCamera::PickCell(const MousePointerState &pointer) const {
    if (pointer.FramebufferSize[0] <= 0 || pointer.FramebufferSize[1] <= 0)
        return std::nullopt;
    // A queued click can predate a resize; projection and scaling use its own dimensions.
    return PickGridCell(GetViewForFramebuffer(pointer.FramebufferSize), pointer.Position, pointer.WindowSize, pointer.FramebufferSize);
}
} // namespace pixel
