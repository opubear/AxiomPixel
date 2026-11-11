#include "BodyComponent.h"

namespace pixel {
void BodyComponent::SetPosition(WorldPosition position) {
    Motion.RestoreState({position});
}

bool BodyComponent::Step(WorldMapView terrain, PlayerInput input) {
    // Anchored targets keep their body and hitbox even when support is mined
    // away, water surrounds them or solid terrain overlaps them.
    if (MotionMode == BodyMotion::Anchored) return false;
    if (!Motion.HasTerrain(terrain)) return false;
    Motion.Tick(1.f / 60, terrain, input.Horizontal, input.Jump);
    return true;
}
} // namespace pixel
