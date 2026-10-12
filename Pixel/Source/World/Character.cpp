#include "Character.h"
#include <algorithm>
#include <cmath>

namespace pixel {
bool Character::Occupies(int x, int y) const {
    return x + 1 > X && x < X + Width && y + 1 > Y && y < Y + Height;
}
bool Character::Collides(WorldStateView world, float x, float y) const {
    if (x < 0 || y < 0 || x + Width > world.Width || y + Height > world.Height)
        return true;
    for (int row = int(std::floor(y)); row < int(std::ceil(y + Height)); ++row)
        for (int column = int(std::floor(x)); column < int(std::ceil(x + Width)); ++column)
            if (world.Cells[size_t(row) * world.Width + column].Type != CellTypes::Empty)
                return true;
    return false;
}
void Character::MoveAxis(WorldStateView world, float distance, bool vertical) {
    // Sub-cell sweeps prevent tunnelling through even a one-cell platform.
    const int steps = std::max(1, int(std::ceil(std::abs(distance) / 0.25f)));
    const float step = distance / steps;
    for (int i = 0; i < steps; ++i) {
        const float x = X + (vertical ? 0 : step);
        const float y = Y + (vertical ? step : 0);
        if (Collides(world, x, y)) {
            // Find the contact point without assuming an integer starting position.
            float low = 0, high = 1;
            for (int iteration = 0; iteration < 16; ++iteration) {
                const float middle = (low + high) * 0.5f;
                if (Collides(world, X + (vertical ? 0 : step * middle), Y + (vertical ? step * middle : 0)))
                    high = middle;
                else
                    low = middle;
            }
            (vertical ? Y : X) += step * low;
            if (vertical)
                VerticalSpeed = 0;
            return;
        }
        X = x;
        Y = y;
    }
}
void Character::Tick(float duration, WorldStateView world, int horizontal, bool jump) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    // Terrain may move into the body: resolve upward without deleting material.
    if (Collides(world, X, Y)) {
        const float original = Y;
        while (Y > 0 && Collides(world, X, Y))
            Y = std::max(0.f, std::floor(Y) - 1);
        if (Collides(world, X, Y)) {
            Y = original;
            Grounded = false;
            return;
        }
        VerticalSpeed = 0;
    }
    Grounded = Collides(world, X, Y + 0.01f);
    if (jump && Grounded)
        VerticalSpeed = -32;
    float remaining = std::min(duration, 0.25f);
    while (remaining > 0) {
        const float dt = std::min(remaining, 1.f / 120);
        MoveAxis(world, std::clamp(horizontal, -1, 1) * 16.f * dt, false);
        VerticalSpeed = std::min(VerticalSpeed + 90 * dt, 80.f);
        MoveAxis(world, VerticalSpeed * dt, true);
        remaining -= dt;
    }
    Grounded = VerticalSpeed >= 0 && Collides(world, X, Y + 0.01f);
}
} // namespace pixel
