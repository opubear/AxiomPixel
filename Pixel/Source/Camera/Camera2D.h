#pragma once
#include "World/WorldCoordinates.h"

namespace pixel {
// Camera centre in world cells, independent of zoom, viewport and rendering.
class Camera2D {
  public:
    using Position = std::array<double, 2>;
    const Position &GetPosition() const { return Centre; }
    const WorldPosition &GetWorldPosition() const { return WorldCentre; }
    Position RelativeTo(CellCoord origin) const { return WorldCentre.RelativeTo(origin); }
    void SetPosition(Position position);
    void SetWorldPosition(WorldPosition position);
    void Translate(Position displacement);
    double GetFollowRate() const { return FollowRate; }
    void SetFollowRate(double rate);
    // dC/dt = rate * (target - C); exact integration for a stationary target.
    void Follow(Position target, double duration);
    void FollowWorld(WorldPosition target, double duration);

  private:
    Position Centre{};
    WorldPosition WorldCentre;
    double FollowRate = 8;
};
} // namespace pixel
