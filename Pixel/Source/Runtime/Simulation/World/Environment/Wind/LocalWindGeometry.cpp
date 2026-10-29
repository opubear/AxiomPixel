#include "LocalWindGeometry.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixel {
performance::MemoryUsage LocalWindGeometry::GetMemoryUsage() const {
    return performance::MeasureObject(*this, Solid, Nearest, Queue);
}

LocalWindGeometry::LocalWindGeometry(uint32_t width, uint32_t height, const LocalWindSettings &settings, WindBoundaryMode boundary)
    : Width(width), Height(height), Settings(settings), Boundary(boundary) {
    if (!IsValidWindBoundary(boundary) || !width || !height || width > 2048 || height > 2048 || !std::isfinite(settings.Radius) || settings.Radius < 1 || settings.Radius > 32
        || !std::isfinite(settings.Deflection) || settings.Deflection < 0 || settings.Deflection > 1
        || !std::isfinite(settings.WakeStrength) || settings.WakeStrength < 0 || settings.WakeStrength > 1)
        throw std::invalid_argument("invalid local wind geometry settings");
    Solid.Reset(width, height);
    Nearest.Reset(width, height, -1);
    Queue.reserve(Solid.size());
}
bool LocalWindGeometry::IsSolid(uint32_t x, uint32_t y) const { return x < Width && y < Height && Solid[size_t(y) * Width + x]; }
void LocalWindGeometry::Update(WorldMapView world) {
    if (!world.IsValid() || world.Width != Width || world.Height < Height || world.CellCount() != size_t(world.Width) * world.Height)
        throw std::invalid_argument("local wind geometry requires complete matching world");
    bool changed = false;
    for (size_t i = 0; i < Solid.size(); ++i) {
        const bool solid = world.MaterialAtIndex(i) != CellTypes::Empty;
        changed |= Solid[i] != solid;
        Solid[i] = solid;
    }
    if (changed)
        RebuildDistance();
}
void LocalWindGeometry::RebuildDistance() {
    Nearest.Fill(-1);
    Queue.clear();
    for (uint32_t i = 0; i < Solid.size(); ++i)
        if (Solid[i]) {
            Nearest[i] = int32_t(i);
            Queue.push_back(i);
        }
    // Multi-source 8-neighbour distance bands. Each pixel is visited
    // once, giving linear work even for dense debris; distance is approximate.
    for (size_t head = 0; head < Queue.size(); ++head) {
        const auto i = Queue[head];
        const int x = int(i % Width), y = int(i / Width);
        const auto source = uint32_t(Nearest[i]);
        const double dx = WindDistance(double(x) - source % Width, double(Width), Boundary);
        const double dy = WindDistance(double(y) - source / Width, double(Height), Boundary);
        if (std::max(std::abs(dx), std::abs(dy)) >= Settings.Radius)
            continue;
        for (int oy = -1; oy <= 1; ++oy)
            for (int ox = -1; ox <= 1; ++ox) {
                int nx = x + ox, ny = y + oy;
                if (Boundary == WindBoundaryMode::Periodic) {
                    nx = (nx + int(Width)) % int(Width);
                    ny = (ny + int(Height)) % int(Height);
                } else if (nx < 0 || ny < 0 || nx >= int(Width) || ny >= int(Height)) {
                    continue;
                }
                const auto j = size_t(ny) * Width + size_t(nx);
                if (Nearest[j] < 0) {
                    Nearest[j] = int32_t(source);
                    Queue.push_back(j);
                }
            }
    }
}
WindVelocity LocalWindGeometry::Constrain(float x, float y, WindVelocity incoming) const {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height)
        return {};
    const auto i = size_t(y) * Width + uint32_t(x);
    if (Solid[i])
        return {};
    if (Nearest[i] < 0)
        return incoming;
    const auto source = uint32_t(Nearest[i]);
    const float dx = float(WindDistance(double(x) - (source % Width + .5), double(Width), Boundary));
    const float dy = float(WindDistance(double(y) - (source / Width + .5), double(Height), Boundary));
    const float distance = std::hypot(dx, dy);
    if (distance <= 0 || distance >= Settings.Radius)
        return incoming;
    const float nx = dx / distance, ny = dy / distance;
    const float weight = std::pow(1 - distance / Settings.Radius, 2);
    const float normalSpeed = incoming.X * nx + incoming.Y * ny;
    if (normalSpeed < 0) {
        // Turn incoming air along the local surface; strongest at the boundary.
        const float correction = -normalSpeed * weight * (1 + Settings.Deflection);
        incoming.X += correction * nx;
        incoming.Y += correction * ny;
    } else {
        const float attenuation = 1 - Settings.WakeStrength * weight;
        incoming.X *= attenuation;
        incoming.Y *= attenuation;
    }
    return incoming;
}
} // namespace pixel
