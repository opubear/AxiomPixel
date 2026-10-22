#include "GameCamera.h"
#include "PixelRender/PixelWorldPresentation.h"
#include <algorithm>
#include <cmath>
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
    WorldPosition position;
    position.Chunk = ChunkOf({view.WorldOriginCells[0], view.WorldOriginCells[1]});
    position.Local = {double(view.WorldOriginCells[0] - position.Chunk.X * storage::ChunkEdge) + (double(view.OriginPixels[0]) + FramebufferSize[0] / 2.0) / view.CellSizePixels[0],
                      double(view.WorldOriginCells[1] - position.Chunk.Y * storage::ChunkEdge) + (double(view.OriginPixels[1]) + FramebufferSize[1] / 2.0) / view.CellSizePixels[1]};
    Camera.SetWorldPosition(position);
    ViewSettings = view;
}
void GameCamera::Reset(WorldRect bounds, const Character *player) {
    ViewSettings.CellSizePixels = {16, 16};
    const auto chunk = ChunkOf({bounds.X, bounds.Y});
    WorldPosition position{chunk,
        {double(bounds.X - chunk.X * storage::ChunkEdge) + bounds.Width / 2.0,
         double(bounds.Y - chunk.Y * storage::ChunkEdge) + bounds.Height / 2.0}};
    if (player) {
        position = player->GetWorldPosition();
        position.Local[0] += Character::Width / 2;
        position.Local[1] += Character::Height / 2;
    }
    Camera.SetWorldPosition(position);
}
void GameCamera::FollowPlayer(const Character *player, float duration) {
    if (!player) return;
    auto position = player->GetWorldPosition();
    position.Local[0] += Character::Width / 2;
    position.Local[1] += Character::Height / 2;
    Camera.FollowWorld(position, std::min(duration, .25f));
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
    view.WorldOriginCells[1] += storage::ChunkEdge;
    SetView(view);
}
std::optional<std::array<int64_t, 2>> GameCamera::PickCell(const MousePointerState &pointer) const {
    if (pointer.FramebufferSize[0] <= 0 || pointer.FramebufferSize[1] <= 0)
        return std::nullopt;
    // A queued click can predate a resize; projection and scaling use its own dimensions.
    return PickGridCell(GetViewForFramebuffer(pointer.FramebufferSize), pointer.Position, pointer.WindowSize, pointer.FramebufferSize);
}

std::optional<WorldPosition> GameCamera::PickWorldPosition(const MousePointerState &pointer) const {
    auto position = Camera.GetWorldPosition();
    for (size_t axis = 0; axis < 2; ++axis) {
        if (pointer.WindowSize[axis] <= 0 || pointer.FramebufferSize[axis] <= 0
            || !std::isfinite(pointer.Position[axis]) || pointer.Position[axis] < 0
            || pointer.Position[axis] >= pointer.WindowSize[axis])
            return std::nullopt;
        const double pixel = pointer.Position[axis] * pointer.FramebufferSize[axis] / pointer.WindowSize[axis];
        position.Local[axis] += (pixel - pointer.FramebufferSize[axis] / 2.0) / ViewSettings.CellSizePixels[axis];
    }
    position.Normalize();
    return position;
}
} // namespace pixel
