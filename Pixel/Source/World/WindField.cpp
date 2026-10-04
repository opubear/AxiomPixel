#include "WindField.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>
#include <utility>

namespace pixel {
namespace {
constexpr float InputSpeedLimit = 1000;
bool ValidVelocity(WindVelocity velocity) {
    return std::isfinite(velocity.X) && std::isfinite(velocity.Y)
        && std::abs(velocity.X) <= InputSpeedLimit && std::abs(velocity.Y) <= InputSpeedLimit;
}
float Wrap(float coordinate, uint32_t extent) {
    const float wrapped = std::fmod(coordinate, float(extent));
    return wrapped < 0 ? wrapped + float(extent) : wrapped;
}
} // namespace

void WindField::Reset(uint32_t width, uint32_t endRow, const WindSettings &settings) {
    if (!width || !endRow || settings.Columns < 2 || settings.Columns > 128 || settings.Rows < 2 || settings.Rows > 128
        || !ValidVelocity(settings.Ambient) || !std::isfinite(settings.GustSpeed) || settings.GustSpeed < 0 || settings.GustSpeed > InputSpeedLimit
        || !std::isfinite(settings.Viscosity) || settings.Viscosity < 0 || settings.Viscosity > 1000000
        || !std::isfinite(settings.Relaxation) || settings.Relaxation < 0 || settings.Relaxation > 100
        || !std::isfinite(settings.DirtDrag) || settings.DirtDrag < 0 || settings.DirtDrag > 1000)
        throw std::invalid_argument("invalid wind extent, resolution or simulation parameters");
    // Build first so rejected input/allocation failures preserve the running field.
    WindField next;
    next.Settings = settings;
    next.Width = width;
    next.Height = endRow;
    next.SpacingX = next.Width / float(settings.Columns);
    next.SpacingY = next.Height / float(settings.Rows);
    const size_t count = size_t(settings.Columns) * settings.Rows;
    next.Velocity.resize(count);
    next.Previous.resize(count);
    next.Pressure.resize(count);
    next.Divergence.resize(count);
    next.DirtCoverage.resize(count);
    next.DirtVelocitySum.resize(count);
    next.DirtBlockage.resize(count);
    next.PressureMobility.resize(count);
    next.LastDirtRow.resize(count);
    std::mt19937 random(settings.Seed);
    const float tau = 2 * std::numbers::pi_v<float>;
    const float phaseX = tau * float(double(random()) / double(std::mt19937::max()));
    const float phaseY = tau * float(double(random()) / double(std::mt19937::max()));
    // Orthogonal shear waves start divergence-free, then interact by advection.
    for (uint32_t y = 0; y < settings.Rows; ++y)
        for (uint32_t x = 0; x < settings.Columns; ++x)
            next.Velocity[next.Index(x, y)] = {
                settings.Ambient.X + settings.GustSpeed * std::sin(tau * (float(y) + 0.5f) / float(settings.Rows) + phaseX),
                settings.Ambient.Y + settings.GustSpeed * std::sin(tau * (float(x) + 0.5f) / float(settings.Columns) + phaseY)};
    *this = std::move(next);
}

size_t WindField::Index(int x, int y) const {
    const int columns = int(Settings.Columns), rows = int(Settings.Rows);
    return size_t((y % rows + rows) % rows) * columns + size_t((x % columns + columns) % columns);
}

WindVelocity WindField::SampleGrid(const std::vector<WindVelocity> &field, float x, float y) const {
    auto interpolate = [&](float sx, float sy, float WindVelocity::*component) {
        sx = Wrap(sx, Settings.Columns);
        sy = Wrap(sy, Settings.Rows);
        const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
        const float tx = sx - float(ix), ty = sy - float(iy);
        return std::lerp(std::lerp(field[Index(ix, iy)].*component, field[Index(ix + 1, iy)].*component, tx),
                         std::lerp(field[Index(ix, iy + 1)].*component, field[Index(ix + 1, iy + 1)].*component, tx), ty);
    };
    return {interpolate(x, y - 0.5f, &WindVelocity::X), interpolate(x - 0.5f, y, &WindVelocity::Y)};
}

WindVelocity WindField::Sample(float x, float y) const {
    if (!IsEnabled() || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height)
        return {};
    return SampleGrid(Velocity, x / SpacingX, y / SpacingY);
}

void WindField::AddImpulse(float x, float y, float radius, WindVelocity delta) {
    if (!IsEnabled() || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height
        || !std::isfinite(radius) || radius <= 0 || !ValidVelocity(delta))
        throw std::invalid_argument("wind impulse requires an active region, valid position, radius and velocity");
    auto weight = [&](float px, float py) {
        const double dx = std::remainder(double(px) - x, double(Width)) / radius;
        const double dy = std::remainder(double(py) - y, double(Height)) / radius;
        const double falloff = std::max(0.0, 1 - dx * dx - dy * dy);
        return float(falloff * falloff);
    };
    auto apply = [](float &velocity, float increment) {
        // Ambient + gust may already exceed the impulse limit. Leave untouched
        // components unchanged, including zero impulses and out-of-radius faces.
        if (increment != 0)
            velocity = std::clamp(velocity + increment, -InputSpeedLimit, InputSpeedLimit);
    };
    for (uint32_t row = 0; row < Settings.Rows; ++row)
        for (uint32_t col = 0; col < Settings.Columns; ++col) {
            auto &v = Velocity[Index(col, row)];
            // Bound repeated user impulses; the next Step projects them.
            apply(v.X, delta.X * weight(float(col) * SpacingX, (float(row) + 0.5f) * SpacingY));
            apply(v.Y, delta.Y * weight((float(col) + 0.5f) * SpacingX, float(row) * SpacingY));
        }
}

void WindField::Step(float seconds) {
    if (!IsEnabled() || !std::isfinite(seconds) || seconds <= 0)
        return;
    seconds = std::min(seconds, 0.25f);
    const float retention = std::exp(-Settings.Relaxation * seconds);
    for (auto &v : Velocity) {
        v.X = std::lerp(Settings.Ambient.X, v.X, retention);
        v.Y = std::lerp(Settings.Ambient.Y, v.Y, retention);
    }
    Diffuse(seconds);
    Project();
    Advect(seconds);
    Project();
}

void WindField::AccumulateDirt(uint32_t x, uint32_t y, float coverage, WindVelocity velocity) {
    // Rasterize the whole pixel footprint, not only its centre. Overlap areas
    // give the same full coverage on coarse, anisotropic and sub-pixel grids.
    const double left = double(x) * Settings.Columns / Width;
    const double right = (double(x) + 1) * Settings.Columns / Width;
    const double top = double(y) * Settings.Rows / Height;
    const double bottom = (double(y) + 1) * Settings.Rows / Height;
    const int endX = std::min(int(Settings.Columns), int(std::ceil(right)));
    const int endY = std::min(int(Settings.Rows), int(std::ceil(bottom)));
    for (int row = int(top); row < endY; ++row) {
        const float overlapY = float(std::min(bottom, double(row + 1)) - std::max(top, double(row)));
        const auto column = size_t(row) * Width + x;
        const bool newColumn = coverage != 0 && !OccupiedColumns[column];
        if (newColumn)
            OccupiedColumns[column] = 1;
        for (int col = int(left); col < endX; ++col) {
            const float overlapX = float(std::min(right, double(col + 1)) - std::max(left, double(col)));
            const float overlap = overlapX * overlapY;
            const auto i = Index(col, row);
            DirtCoverage[i] += overlap * coverage;
            DirtVelocitySum[i].X += overlap * velocity.X;
            DirtVelocitySum[i].Y += overlap * velocity.Y;
            // A thin vertical stack can obstruct an entire horizontal channel.
            // Use the union of projected strips, not pixel area / grid-cell area.
            if (coverage != 0 && LastDirtRow[i] != y) {
                DirtBlockage[i].X += overlapY;
                LastDirtRow[i] = y;
            }
            if (newColumn)
                DirtBlockage[i].Y += overlapX;
        }
    }
}

void WindField::CoupleDirt(WorldStateView world, std::span<const DirtMotion> motion, float seconds) {
    if (!IsEnabled() || Settings.DirtDrag == 0 || !std::isfinite(seconds) || seconds <= 0)
        return;
    if (world.Width != Width || world.Height < Height || uint64_t(world.Width) * world.Height != world.Cells.size())
        throw std::invalid_argument("dirt feedback requires the wind's world extent and complete cell storage");
    for (const auto &moved : motion)
        if (moved.X >= world.Width || moved.Y >= world.Height || !ValidVelocity(moved.Velocity)
            || world.Cells[size_t(moved.Y) * world.Width + moved.X].Type != CellTypes::Dirt)
            throw std::invalid_argument("dirt motion must refer to a final occupied dirt cell with finite velocity");
    std::fill(DirtCoverage.begin(), DirtCoverage.end(), 0);
    std::fill(DirtVelocitySum.begin(), DirtVelocitySum.end(), WindVelocity{});
    std::fill(DirtBlockage.begin(), DirtBlockage.end(), WindVelocity{});
    std::fill(LastDirtRow.begin(), LastDirtRow.end(), UINT32_MAX);
    OccupiedColumns.assign(size_t(Width) * Settings.Rows, 0);
    bool occupied = false;
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < world.Width; ++x)
            if (world.Cells[size_t(y) * world.Width + x].Type == CellTypes::Dirt) {
                AccumulateDirt(x, y, 1, {});
                occupied = true;
            }
    if (!occupied)
        return; // Empty worlds preserve the original solver exactly.
    for (const auto &moved : motion)
        if (moved.Y < Height)
            AccumulateDirt(moved.X, moved.Y, 0, moved.Velocity);
    const float dragStep = Settings.DirtDrag * std::min(seconds, 0.25f);
    auto coupleFace = [&](float &air, float coverage, float velocitySum, float blockage) {
        if (coverage <= 0)
            return 1.f;
        const float target = velocitySum / coverage;
        // Finite permeability keeps the pressure system connected even with
        // packed/moving dirt. The same response MUST weight pressure gradients.
        const float response = std::max(0.001f, std::exp(-dragStep * std::clamp(0.5f * blockage, 0.f, 1.f)));
        air = std::lerp(target, air, response);
        return response;
    };
    for (int y = 0; y < int(Settings.Rows); ++y)
        for (int x = 0; x < int(Settings.Columns); ++x) {
            const auto i = Index(x, y), left = Index(x - 1, y), up = Index(x, y - 1);
            PressureMobility[i].X = coupleFace(Velocity[i].X, DirtCoverage[i] + DirtCoverage[left], DirtVelocitySum[i].X + DirtVelocitySum[left].X,
                                               DirtBlockage[i].X + DirtBlockage[left].X);
            PressureMobility[i].Y = coupleFace(Velocity[i].Y, DirtCoverage[i] + DirtCoverage[up], DirtVelocitySum[i].Y + DirtVelocitySum[up].Y,
                                               DirtBlockage[i].Y + DirtBlockage[up].Y);
        }
    // Propagate the disturbance into surrounding air and remove drag-induced divergence.
    Project(PressureMobility);
}

void WindField::Diffuse(float seconds) {
    if (Settings.Viscosity == 0)
        return;
    Previous = Velocity;
    const float ax = seconds * Settings.Viscosity / (SpacingX * SpacingX);
    const float ay = seconds * Settings.Viscosity / (SpacingY * SpacingY);
    const float denominator = 1 + 2 * (ax + ay);
    // Implicit diffusion: (I - dt * viscosity * Laplacian) u = u_previous.
    for (int iteration = 0; iteration < 20; ++iteration)
        for (int y = 0; y < int(Settings.Rows); ++y)
            for (int x = 0; x < int(Settings.Columns); ++x) {
                const auto i = Index(x, y);
                const auto left = Velocity[Index(x - 1, y)], right = Velocity[Index(x + 1, y)];
                const auto up = Velocity[Index(x, y - 1)], down = Velocity[Index(x, y + 1)];
                Velocity[i] = {(Previous[i].X + ax * (left.X + right.X) + ay * (up.X + down.X)) / denominator,
                               (Previous[i].Y + ax * (left.Y + right.Y) + ay * (up.Y + down.Y)) / denominator};
            }
}

void WindField::Advect(float seconds) {
    Previous = Velocity;
    for (int y = 0; y < int(Settings.Rows); ++y)
        for (int x = 0; x < int(Settings.Columns); ++x) {
            const float fx = float(x), fy = float(y);
            const auto atXFace = SampleGrid(Previous, fx, fy + 0.5f);
            const auto atYFace = SampleGrid(Previous, fx + 0.5f, fy);
            // Backtrace each staggered face through the same old velocity field.
            Velocity[Index(x, y)] = {
                SampleGrid(Previous, fx - seconds * atXFace.X / SpacingX, fy + 0.5f - seconds * atXFace.Y / SpacingY).X,
                SampleGrid(Previous, fx + 0.5f - seconds * atYFace.X / SpacingX, fy - seconds * atYFace.Y / SpacingY).Y};
        }
}

void WindField::Project(std::span<const WindVelocity> mobility) {
    const float invX = 1 / SpacingX, invY = 1 / SpacingY;
    const float ax = invX * invX, ay = invY * invY;
    std::fill(Pressure.begin(), Pressure.end(), 0);
    double sum = 0;
    for (int y = 0; y < int(Settings.Rows); ++y)
        for (int x = 0; x < int(Settings.Columns); ++x) {
            const auto i = Index(x, y);
            Divergence[i] = (Velocity[Index(x + 1, y)].X - Velocity[i].X) * invX
                          + (Velocity[Index(x, y + 1)].Y - Velocity[i].Y) * invY;
            sum += Divergence[i];
        }
    // A periodic Poisson problem requires a zero-mean right-hand side.
    const float mean = float(sum / double(Divergence.size()));
    for (auto &div : Divergence)
        div -= mean;
    auto response = [&](size_t i) { return mobility.empty() ? WindVelocity{1, 1} : mobility[i]; };
    // Solve div(M grad(p)) = div(u*), then u = u* - M grad(p). Using an
    // unweighted solve here would undo the directional obstacle response.
    for (int iteration = 0; iteration < 80; ++iteration)
        for (int y = 0; y < int(Settings.Rows); ++y)
            for (int x = 0; x < int(Settings.Columns); ++x) {
                const auto i = Index(x, y), right = Index(x + 1, y), down = Index(x, y + 1);
                const auto m = response(i);
                const float leftWeight = ax * m.X, rightWeight = ax * response(right).X;
                const float upWeight = ay * m.Y, downWeight = ay * response(down).Y;
                Pressure[i] = (leftWeight * Pressure[Index(x - 1, y)] + rightWeight * Pressure[right]
                               + upWeight * Pressure[Index(x, y - 1)] + downWeight * Pressure[down] - Divergence[i])
                            / (leftWeight + rightWeight + upWeight + downWeight);
            }
    for (int y = 0; y < int(Settings.Rows); ++y)
        for (int x = 0; x < int(Settings.Columns); ++x) {
            const auto i = Index(x, y);
            const auto m = response(i);
            Velocity[i].X -= m.X * (Pressure[i] - Pressure[Index(x - 1, y)]) * invX;
            Velocity[i].Y -= m.Y * (Pressure[i] - Pressure[Index(x, y - 1)]) * invY;
        }
}

float WindField::RmsDivergence() const {
    if (!IsEnabled())
        return 0;
    double sum = 0;
    for (int y = 0; y < int(Settings.Rows); ++y)
        for (int x = 0; x < int(Settings.Columns); ++x) {
            const auto i = Index(x, y);
            const double div = (Velocity[Index(x + 1, y)].X - Velocity[i].X) / SpacingX
                             + (Velocity[Index(x, y + 1)].Y - Velocity[i].Y) / SpacingY;
            sum += div * div;
        }
    return float(std::sqrt(sum / double(Velocity.size())));
}
} // namespace pixel
