#pragma once
#include "Camera2D.h"
#include "Input/MousePointerState.h"
#include "PixelRender/WorldGridFrame.h"
#include "Actors/Character.h"

namespace pixel {
// Pixel camera policy: zoom, viewport projection, player following and free pan.
class GameCamera {
  public:
    GameCamera();
    Camera2D &GetCamera() { return Camera; }
    const Camera2D &GetCamera() const { return Camera; }
    void SetFramebufferSize(std::array<int, 2> size) { FramebufferSize = size; }
    std::array<int, 2> GetFramebufferSize() const { return FramebufferSize; }
    GridView2D GetView() const;
    GridView2D GetViewForFramebuffer(std::array<int, 2> size) const;
    void SetView(const GridView2D &view);
    void Reset(WorldRect bounds, const Character *player);
    void FollowPlayer(const Character *player, float duration);
    void Pan(std::array<int, 2> direction, float duration);
    void Zoom(bool in);
    void NextRegion();
    std::optional<std::array<int64_t, 2>> PickCell(const MousePointerState &pointer) const;
    std::optional<WorldPosition> PickWorldPosition(const MousePointerState &pointer) const;

  private:
    Camera2D Camera;
    GridView2D ViewSettings;
    std::array<int, 2> FramebufferSize{};
};
} // namespace pixel
