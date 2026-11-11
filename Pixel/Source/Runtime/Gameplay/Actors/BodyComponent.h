#pragma once
#include "Body.h"
#include "Component.h"
#include "PlayerInput.h"

namespace pixel {
enum class BodyMotion { Dynamic, Anchored };

class BodyComponent final : public Component {
  public:
    BodyComponent(Actor &owner, Body body, BodyMotion motion = BodyMotion::Dynamic)
        : Component(owner), Motion(body), MotionMode(motion) {}

    const Body &GetBody() const { return Motion; }
    BodyMotion GetMotionMode() const { return MotionMode; }
    void SetPosition(WorldPosition position);
    bool Step(WorldMapView terrain, PlayerInput input);
    void MoveMonster(WorldMapView terrain, int horizontal, bool jump, float speed) {
        Motion.Tick(1.f / 60, terrain, horizontal, jump, speed);
    }
    void FlyMonster(WorldMapView terrain, float velocityX, float velocityY) {
        Motion.Fly(1.f / 60, terrain, velocityX, velocityY);
    }

  private:
    Body Motion;
    BodyMotion MotionMode;
};
} // namespace pixel
