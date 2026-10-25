#include "Camera2D.h"
#include <cmath>
#include <stdexcept>

namespace pixel {
namespace {
void ValidatePosition(Camera2D::Position position) {
    for (double value : position)
        if (!std::isfinite(value))
            throw std::invalid_argument("camera position must be finite");
}
}
void Camera2D::SetPosition(Position position) {
    ValidatePosition(position);
    SetWorldPosition(WorldPosition::FromAbsolute(position[0], position[1]));
}
void Camera2D::SetWorldPosition(WorldPosition position) {
    position.Normalize();
    WorldCentre = position;
    Centre = position.RelativeTo({});
}
void Camera2D::Translate(Position displacement) {
    ValidatePosition(displacement);
    auto next = WorldCentre;
    next.Local[0] += displacement[0];
    next.Local[1] += displacement[1];
    SetWorldPosition(next);
}
void Camera2D::SetFollowRate(double rate) {
    if (!std::isfinite(rate) || rate < 0)
        throw std::invalid_argument("camera follow rate must be finite and nonnegative");
    FollowRate = rate;
}
void Camera2D::Follow(Position target, double duration) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    ValidatePosition(target);
    FollowWorld(WorldPosition::FromAbsolute(target[0], target[1]), duration);
}
void Camera2D::FollowWorld(WorldPosition target, double duration) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    target.Normalize();
    const CellCoord origin{WorldCentre.Chunk.X * storage::ChunkEdge, WorldCentre.Chunk.Y * storage::ChunkEdge};
    const auto relative = target.RelativeTo(origin);
    const double blend = -std::expm1(-FollowRate * duration);
    auto next = WorldCentre;
    for (size_t axis = 0; axis < next.Local.size(); ++axis)
        next.Local[axis] = std::lerp(next.Local[axis], relative[axis], blend);
    SetWorldPosition(next);
}
} // namespace pixel
