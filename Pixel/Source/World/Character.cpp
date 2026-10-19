#include "Character.h"
#include <algorithm>
#include <cmath>

namespace pixel {
bool Character::Occupies(int64_t x, int64_t y) const {
    const auto origin = CellOrigin();
    const double localX = double(x - origin.X), localY = double(y - origin.Y);
    return localX + 1 > X && localX < X + Width && localY + 1 > Y && localY < Y + Height;
}

bool Character::Collides(WorldMapView world, double x, double y) const {
    const auto origin = CellOrigin();
    const auto firstX = int64_t(std::floor(x)), firstY = int64_t(std::floor(y));
    const auto lastX = int64_t(std::ceil(x + Width)), lastY = int64_t(std::ceil(y + Height));
    for (int64_t row = firstY; row < lastY; ++row) {
        for (int64_t column = firstX; column < lastX; ++column) {
            const CellCoord cell{origin.X + column, origin.Y + row};
            const int64_t vx = cell.X - world.Origin.X, vy = cell.Y - world.Origin.Y;
            if (vx < 0 || vy < 0 || vx >= world.Width || vy >= world.Height)
                return true;
            const auto material = world.TryAtLocal(uint32_t(vx), uint32_t(vy));
            if (!material || material->Type != CellTypes::Empty)
                return true;
        }
    }
    return false;
}

void Character::MoveAxis(WorldMapView world, double distance, bool vertical) {
    const int steps = std::max(1, int(std::ceil(std::abs(distance) / 0.25)));
    const double step = distance / steps;
    for (int i = 0; i < steps; ++i) {
        const double x = X + (vertical ? 0 : step);
        const double y = Y + (vertical ? step : 0);
        if (Collides(world, x, y)) {
            double low = 0, high = 1;
            for (int iteration = 0; iteration < 20; ++iteration) {
                const double middle = (low + high) * .5;
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

void Character::Tick(float duration, WorldMapView world, int horizontal, bool jump) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    if (Collides(world, X, Y)) {
        const double original = Y;
        const double top = double(world.Origin.Y - CellOrigin().Y);
        while (Y > top && Collides(world, X, Y))
            Y = std::max(top, std::floor(Y) - 1);
        if (Collides(world, X, Y)) {
            Y = original;
            Grounded = false;
            return;
        }
        VerticalSpeed = 0;
    }
    Grounded = Collides(world, X, Y + .01);
    if (jump && Grounded)
        VerticalSpeed = -32;
    double remaining = std::min(double(duration), .25);
    while (remaining > 0) {
        const double dt = std::min(remaining, 1.0 / 120);
        MoveAxis(world, std::clamp(horizontal, -1, 1) * 16.0 * dt, false);
        VerticalSpeed = std::min(VerticalSpeed + 90 * dt, 80.0);
        MoveAxis(world, VerticalSpeed * dt, true);
        remaining -= dt;
    }
    Grounded = VerticalSpeed >= 0 && Collides(world, X, Y + .01);
    auto position = GetWorldPosition();
    position.Normalize();
    OriginChunk = position.Chunk;
    X = position.Local[0];
    Y = position.Local[1];
}
} // namespace pixel
