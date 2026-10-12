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
    Centre = position;
}
void Camera2D::Translate(Position displacement) {
    SetPosition({Centre[0] + displacement[0], Centre[1] + displacement[1]});
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
    // expm1 keeps small timesteps accurate; the factor never exceeds one.
    const double blend = -std::expm1(-FollowRate * duration);
    for (size_t axis = 0; axis < Centre.size(); ++axis)
        Centre[axis] = std::lerp(Centre[axis], target[axis], blend);
}
} // namespace pixel
