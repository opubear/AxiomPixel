#include "WindField.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>
#include <utility>

namespace pixel {
performance::MemoryUsage WindField::GetMemoryUsage() const {
    return performance::MeasureObject(*this, Velocity, Previous, Pressure, Divergence, DirtCoverage, DirtVelocitySum, DirtBlockage, PressureMobility, LastDirtRow, OccupiedColumns, RightU, BottomV);
}

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
    if (!IsValidWindBoundary(settings.Boundary) || !width || !endRow || settings.Columns < 2 || settings.Columns > 128 || settings.Rows < 2 || settings.Rows > 128
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
    next.RightU.assign(settings.Rows, settings.Ambient.X);
    next.BottomV.assign(settings.Columns, settings.Ambient.Y);
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
    next.PrescribeInflow();
    *this = std::move(next);
}

void WindField::MoveDomain(uint32_t width, uint32_t height, int64_t offsetX, int64_t offsetY) {
    WindField next;
    next.Reset(width, height, Settings);
    for (uint32_t y = 0; y < Settings.Rows; ++y)
        for (uint32_t x = 0; x < Settings.Columns; ++x) {
            const float oldX = float(double(x) * next.SpacingX + double(offsetX));
            const float oldY = float(double(y) * next.SpacingY + double(offsetY));
            auto &v = next.Velocity[next.Index(x, y)];
            if (oldX >= 0 && oldX < Width && oldY + .5f * next.SpacingY >= 0 && oldY + .5f * next.SpacingY < Height)
                v.X = SampleGrid(Velocity, oldX / SpacingX, (oldY + .5f * next.SpacingY) / SpacingY).X;
            if (oldX + .5f * next.SpacingX >= 0 && oldX + .5f * next.SpacingX < Width && oldY >= 0 && oldY < Height)
                v.Y = SampleGrid(Velocity, (oldX + .5f * next.SpacingX) / SpacingX, oldY / SpacingY).Y;
        }
    next.Project();
    *this = std::move(next);
}

float WindField::UFace(int x, int y) const {
    return !Periodic() && x >= int(Settings.Columns) ? RightU[size_t(y)] : Velocity[Index(x, y)].X;
}
float WindField::VFace(int x, int y) const {
    return !Periodic() && y >= int(Settings.Rows) ? BottomV[size_t(x)] : Velocity[Index(x, y)].Y;
}
void WindField::PrescribeInflow() {
    if (Periodic())
        return;
    for (uint32_t y = 0; y < Settings.Rows; ++y) {
        if (Inflow(WindBoundarySide::Left))
            Velocity[Index(0, y)].X = Settings.Ambient.X;
        if (Inflow(WindBoundarySide::Right))
            RightU[y] = Settings.Ambient.X;
        else
            RightU[y] = Velocity[Index(Settings.Columns - 1, y)].X;
    }
    for (uint32_t x = 0; x < Settings.Columns; ++x) {
        if (Inflow(WindBoundarySide::Top))
            Velocity[Index(x, 0)].Y = Settings.Ambient.Y;
        if (Inflow(WindBoundarySide::Bottom))
            BottomV[x] = Settings.Ambient.Y;
        else
            BottomV[x] = Velocity[Index(x, Settings.Rows - 1)].Y;
    }
}
void WindField::ProjectOpen(std::span<const WindVelocity> mobility) {
    PrescribeInflow();
    const float ax = 1 / (SpacingX * SpacingX), ay = 1 / (SpacingY * SpacingY);
    const int columns = int(Settings.Columns), rows = int(Settings.Rows);
    auto response = [&](int x, int y, bool horizontal) {
        if (mobility.empty())
            return 1.f;
        const auto m = mobility[Index(x, y)];
        return horizontal ? m.X : m.Y;
    };
    auto weight = [&](int x, int y, int dx, int dy) {
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || nx >= columns || ny < 0 || ny >= rows) {
            const auto side = nx < 0 ? WindBoundarySide::Left : nx >= columns ? WindBoundarySide::Right
                                                          : ny < 0            ? WindBoundarySide::Top
                                                                              : WindBoundarySide::Bottom;
            if (Inflow(side))
                return 0.f;
        }
        return dx ? ax * response(x + (dx > 0), y, true) : ay * response(x, y + (dy > 0), false);
    };
    std::fill(Pressure.begin(), Pressure.end(), 0);
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x)
            Divergence[Index(x, y)] = (UFace(x + 1, y) - UFace(x, y)) / SpacingX
                                    + (VFace(x, y + 1) - VFace(x, y)) / SpacingY;
    // Prescribed inlet flux and zero outlet pressure give a nonsingular system.
    for (int iteration = 0; iteration < 400; ++iteration)
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < columns; ++x) {
                float sum = -Divergence[Index(x, y)], diagonal = 0;
                for (const auto [dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}}) {
                    const float w = weight(x, y, dx, dy);
                    diagonal += w;
                    if (x + dx >= 0 && x + dx < columns && y + dy >= 0 && y + dy < rows)
                        sum += w * Pressure[Index(x + dx, y + dy)];
                }
                Pressure[Index(x, y)] = diagonal ? sum / diagonal : 0;
            }
    for (int y = 0; y < rows; ++y)
        for (int x = 0; x < columns; ++x) {
            const auto i = Index(x, y);
            Velocity[i].X -= weight(x, y, -1, 0) * SpacingX * (Pressure[i] - (x ? Pressure[Index(x - 1, y)] : 0));
            Velocity[i].Y -= weight(x, y, 0, -1) * SpacingY * (Pressure[i] - (y ? Pressure[Index(x, y - 1)] : 0));
            if (x + 1 == columns)
                RightU[size_t(y)] += weight(x, y, 1, 0) * SpacingX * Pressure[i];
            if (y + 1 == rows)
                BottomV[size_t(x)] += weight(x, y, 0, 1) * SpacingY * Pressure[i];
        }
}

size_t WindField::Index(int x, int y) const {
    const int columns = int(Settings.Columns), rows = int(Settings.Rows);
    if (Periodic())
        return size_t((y % rows + rows) % rows) * columns + size_t((x % columns + columns) % columns);
    return size_t(std::clamp(y, 0, rows - 1)) * columns + size_t(std::clamp(x, 0, columns - 1));
}

WindVelocity WindField::SampleGrid(const std::vector<WindVelocity> &field, float x, float y) const {
    if (!Periodic() && ((x < 0 && Inflow(WindBoundarySide::Left)) || (x > Settings.Columns && Inflow(WindBoundarySide::Right)) || (y < 0 && Inflow(WindBoundarySide::Top)) || (y > Settings.Rows && Inflow(WindBoundarySide::Bottom))))
        return Settings.Ambient;
    auto interpolate = [&](float sx, float sy, float WindVelocity::*component) {
        if (Periodic()) {
            sx = Wrap(sx, Settings.Columns);
            sy = Wrap(sy, Settings.Rows);
        } else {
            sx = std::clamp(sx, 0.f, float(Settings.Columns - 1));
            sy = std::clamp(sy, 0.f, float(Settings.Rows - 1));
        }
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
        const double dx = WindDistance(double(px) - x, double(Width), Settings.Boundary) / radius;
        const double dy = WindDistance(double(py) - y, double(Height), Settings.Boundary) / radius;
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

void WindField::CoupleDirt(WorldMapView world, std::span<const DirtMotion> motion, float seconds) {
    if (!IsEnabled() || Settings.DirtDrag == 0 || !std::isfinite(seconds) || seconds <= 0)
        return;
    if (!world.IsValid() || world.Width != Width || world.Height < Height || uint64_t(world.Width) * world.Height != world.CellCount())
        throw std::invalid_argument("dirt feedback requires the wind's world extent and complete cell storage");
    for (const auto &moved : motion)
        if (moved.X >= world.Width || moved.Y >= world.Height || !ValidVelocity(moved.Velocity)
            || world.AtIndex(size_t(moved.Y) * world.Width + moved.X).Type != CellTypes::Dirt)
            throw std::invalid_argument("dirt motion must refer to a final occupied dirt cell with finite velocity");
    std::fill(DirtCoverage.begin(), DirtCoverage.end(), 0);
    std::fill(DirtVelocitySum.begin(), DirtVelocitySum.end(), WindVelocity{});
    std::fill(DirtBlockage.begin(), DirtBlockage.end(), WindVelocity{});
    std::fill(LastDirtRow.begin(), LastDirtRow.end(), UINT32_MAX);
    OccupiedColumns.assign(size_t(Width) * Settings.Rows, 0);
    bool occupied = false;
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < world.Width; ++x)
            if (world.AtIndex(size_t(y) * world.Width + x).Type == CellTypes::Dirt) {
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
    if (!Periodic()) {
        ProjectOpen(mobility);
        return;
    }
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
            const double div = (UFace(x + 1, y) - UFace(x, y)) / SpacingX
                             + (VFace(x, y + 1) - VFace(x, y)) / SpacingY;
            sum += div * div;
        }
    return float(std::sqrt(sum / double(Velocity.size())));
}
} // namespace pixel
