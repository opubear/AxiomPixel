#include "Body.h"
#include <algorithm>
#include <cmath>

namespace pixel {
void Body::RestoreState(BodyState state) {
    if (!std::isfinite(state.VerticalSpeed)) throw std::invalid_argument("invalid actor velocity");
    state.Position.Normalize();
    OriginChunk = state.Position.Chunk;
    X = state.Position.Local[0];
    Y = state.Position.Local[1];
    VerticalSpeed = state.VerticalSpeed;
    Grounded = state.Grounded;
}

// A moving simulation window can cut through a body. Both authority and
// presentation freeze unknown terrain instead of treating it as solid overlap.
bool Body::HasTerrain(WorldMapView terrain) const {
    const auto first = GetWorldPosition().Cell();
    const auto origin = CellOrigin();
    const CellCoord last{origin.X + int64_t(std::ceil(CentimetersToCells(X + Width))) - 1,
                         origin.Y + int64_t(std::ceil(CentimetersToCells(Y + Height))) - 1};
    const WorldRect active{terrain.Origin.X, terrain.Origin.Y, terrain.Width, terrain.Height};
    if (!active.Contains(first) || !active.Contains(last)) return false;
    for (auto y = first.Y; y <= last.Y; ++y)
        for (auto x = first.X; x <= last.X; ++x)
            if (!terrain.TryAtLocal(uint32_t(x - active.X), uint32_t(y - active.Y))) return false;
    return true;
}

bool Body::Occupies(int64_t x, int64_t y) const {
    const auto origin = CellOrigin();
    const float localX = CellsToCentimeters(float(x - origin.X)), localY = CellsToCentimeters(float(y - origin.Y));
    return localX + CentimetersPerCell > X && localX < X + Width
        && localY + CentimetersPerCell > Y && localY < Y + Height;
}

bool Body::Collides(WorldMapView world, float x, float y) const {
    const auto origin = CellOrigin();
    const auto firstX = int64_t(std::floor(CentimetersToCells(x))), firstY = int64_t(std::floor(CentimetersToCells(y)));
    const auto lastX = int64_t(std::ceil(CentimetersToCells(x + Width))), lastY = int64_t(std::ceil(CentimetersToCells(y + Height)));
    for (int64_t row = firstY; row < lastY; ++row) {
        for (int64_t column = firstX; column < lastX; ++column) {
            const CellCoord cell{origin.X + column, origin.Y + row};
            const int64_t vx = cell.X - world.Origin.X, vy = cell.Y - world.Origin.Y;
            if (vx < 0 || vy < 0 || vx >= world.Width || vy >= world.Height)
                return true;
            const auto material = world.TryAtLocal(uint32_t(vx), uint32_t(vy));
            if (!material || BlocksCharacter(material->Type))
                return true;
        }
    }
    return false;
}

float Body::Submersion(WorldMapView world) const {
    const auto origin = CellOrigin();
    float area = 0;
    for (int64_t y = int64_t(std::floor(CentimetersToCells(Y))); y < int64_t(std::ceil(CentimetersToCells(Y + Height))); ++y)
        for (int64_t x = int64_t(std::floor(CentimetersToCells(X))); x < int64_t(std::ceil(CentimetersToCells(X + Width))); ++x) {
            const CellCoord cell{origin.X + x, origin.Y + y};
            const int64_t vx = cell.X - world.Origin.X, vy = cell.Y - world.Origin.Y;
            if (vx < 0 || vy < 0 || vx >= world.Width || vy >= world.Height)
                continue;
            const auto state = world.TryAtLocal(uint32_t(vx), uint32_t(vy));
            if (state && state->Type == CellTypeId::Water)
                area += std::max(0.f, std::min(X + Width, CellsToCentimeters(float(x + 1))) - std::max(X, CellsToCentimeters(float(x))))
                    * std::max(0.f, std::min(Y + Height, CellsToCentimeters(float(y + 1))) - std::max(Y, CellsToCentimeters(float(y))));
        }
    return area / (Width * Height);
}

void Body::MoveAxis(WorldMapView world, float distance, bool vertical) {
    const int steps = std::max(1, int(std::ceil(std::abs(distance) / 12.5f)));
    const float step = distance / steps;
    for (int i = 0; i < steps; ++i) {
        const float x = X + (vertical ? 0 : step);
        const float y = Y + (vertical ? step : 0);
        if (Collides(world, x, y)) {
            float low = 0, high = 1;
            for (int iteration = 0; iteration < 20; ++iteration) {
                const float middle = (low + high) * .5f;
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

void Body::Tick(float duration, WorldMapView world, int horizontal, bool jump) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    if (Collides(world, X, Y)) {
        const float original = Y;
        const float top = CellsToCentimeters(float(world.Origin.Y - CellOrigin().Y));
        while (Y > top && Collides(world, X, Y))
            Y = std::max(top, CellsToCentimeters(std::floor(CentimetersToCells(Y)) - 1));
        if (Collides(world, X, Y)) {
            Y = original;
            Grounded = false;
            return;
        }
        VerticalSpeed = 0;
    }
    Grounded = Collides(world, X, Y + .5f);
    if (jump && (Grounded || Submersion(world) > 0))
        VerticalSpeed = -JumpSpeed;
    float remaining = std::min(duration, .25f);
    while (remaining > 0) {
        const float dt = std::min(remaining, 1.f / 120);
        const float submerged = Submersion(world);
        MoveAxis(world, std::clamp(horizontal, -1, 1) * WalkSpeed * (1 - .4f * submerged) * dt, false);
        VerticalSpeed = std::min((VerticalSpeed + Gravity * (1 - 1.2f * submerged) * dt) * std::exp(-3 * submerged * dt), MaximumFallSpeed);
        MoveAxis(world, VerticalSpeed * dt, true);
        remaining -= dt;
    }
    Grounded = VerticalSpeed >= 0 && Collides(world, X, Y + .5f);
    auto position = GetWorldPosition();
    position.Normalize();
    OriginChunk = position.Chunk;
    X = position.Local[0];
    Y = position.Local[1];
}
} // namespace pixel
