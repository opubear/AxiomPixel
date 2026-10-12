#pragma once
#include <array>

namespace pixel {
// Camera centre in world cells, independent of zoom, viewport and rendering.
class Camera2D {
  public:
    using Position = std::array<double, 2>;
    const Position &GetPosition() const { return Centre; }
    void SetPosition(Position position);
    void Translate(Position displacement);
    double GetFollowRate() const { return FollowRate; }
    void SetFollowRate(double rate);
    // dC/dt = rate * (target - C); exact integration for a stationary target.
    void Follow(Position target, double duration);

  private:
    Position Centre{};
    double FollowRate = 8;
};
} // namespace pixel
