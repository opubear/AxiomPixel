#include "ProceduralWindField.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>

namespace pixel {
performance::MemoryUsage ProceduralWindField::GetMemoryUsage() const {
    return performance::MeasureObject(*this, Impulses);
}

ProceduralWindField::ProceduralWindField(uint32_t width, uint32_t height, const WindSettings &settings)
    : Width(width), Height(height), Settings(settings) {
    // Reuse the established settings contract without retaining an NS backend.
    WindField validation;
    validation.Reset(width, height, settings);
    if (width > 2048 || height > 2048)
        throw std::invalid_argument("procedural wind dimensions must be <= 2048");
    Impulses.resize(size_t(width) * height);
    std::mt19937 random(settings.Seed);
    Phase = double(random()) / std::mt19937::max() * 2 * std::numbers::pi;
}
void ProceduralWindField::MoveDomain(uint32_t width, uint32_t height, CellCoord origin) {
    std::vector<WindVelocity> next(size_t(width) * height);
    const int64_t dx = WindCoordinateOffset(origin.X, Origin.X);
    const int64_t dy = WindCoordinateOffset(origin.Y, Origin.Y);
    if (dx < int64_t(Width) && dy < int64_t(Height) && dx > -int64_t(width) && dy > -int64_t(height))
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x) {
                const int64_t oldX = dx + x, oldY = dy + y;
                if (oldX >= 0 && oldY >= 0 && oldX < int64_t(Width) && oldY < int64_t(Height))
                    next[size_t(y) * width + x] = Impulses[size_t(oldY) * Width + size_t(oldX)];
            }
    Width = width;
    Height = height;
    Origin = origin;
    Impulses = std::move(next);
}
void ProceduralWindField::Step(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    const double dt = std::min(double(seconds), .25);
    Time += dt;
    const float decay = float(std::exp(-Settings.Relaxation * dt));
    for (auto &v : Impulses) {
        v.X *= decay;
        v.Y *= decay;
    }
}
WindVelocity ProceduralWindField::Sample(float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height)
        return {};
    const double tau = 2 * std::numbers::pi;
    const auto impulse = Impulses[size_t(y) * Width + uint32_t(x)];
    return {Settings.Ambient.X + Settings.GustSpeed * float(std::sin(tau * (double(Origin.Y % int64_t(GustWavelength)) + y) / GustWavelength + Phase + .2 * Time)) + impulse.X,
            Settings.Ambient.Y + Settings.GustSpeed * float(std::sin(tau * (double(Origin.X % int64_t(GustWavelength)) + x) / GustWavelength - Phase - .1 * Time)) + impulse.Y};
}
void ProceduralWindField::AddImpulse(float x, float y, float radius, WindVelocity delta) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height || !std::isfinite(radius) || radius <= 0
        || !std::isfinite(delta.X) || !std::isfinite(delta.Y) || std::abs(delta.X) > 1000 || std::abs(delta.Y) > 1000)
        throw std::invalid_argument("invalid procedural wind impulse");
    for (uint32_t py = 0; py < Height; ++py)
        for (uint32_t px = 0; px < Width; ++px) {
            const double dx = WindDistance(px + .5 - x, double(Width), Settings.Boundary) / radius;
            const double dy = WindDistance(py + .5 - y, double(Height), Settings.Boundary) / radius;
            const float weight = float(std::pow(std::max(0.0, 1 - dx * dx - dy * dy), 2));
            auto &v = Impulses[size_t(py) * Width + px];
            v.X = std::clamp(v.X + delta.X * weight, -1000.f, 1000.f);
            v.Y = std::clamp(v.Y + delta.Y * weight, -1000.f, 1000.f);
        }
}
} // namespace pixel
