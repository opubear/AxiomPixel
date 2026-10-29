#include "LbmWindField.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <stdexcept>

namespace pixel {
performance::MemoryUsage LbmWindField::GetMemoryUsage() const {
    return performance::MeasureObject(*this, Solid, F, Next, Velocity);
}

namespace {
constexpr std::array<int, 9> Cx{0, 1, 0, -1, 0, 1, -1, -1, 1};
constexpr std::array<int, 9> Cy{0, 0, 1, 0, -1, 1, 1, -1, -1};
constexpr std::array<int, 9> Opposite{0, 3, 4, 1, 2, 7, 8, 5, 6};
constexpr std::array<double, 9> Weight{4. / 9, 1. / 9, 1. / 9, 1. / 9, 1. / 9, 1. / 36, 1. / 36, 1. / 36, 1. / 36};
} // namespace
LbmWindField::LbmWindField(WorldMapView world, uint32_t height, const LbmWindSettings &settings)
    : Width(world.Width), Height(height), Settings(settings) {
    if (!IsValidWindBoundary(settings.Boundary) || !world.IsValid() || !Width || !Height || Width > 2048 || Height > 2048 || world.Height < Height || world.CellCount() != size_t(Width) * world.Height
        || !std::isfinite(settings.LatticeSeconds) || settings.LatticeSeconds < 1.0 / 4000 || settings.LatticeSeconds > 1.0 / 60
        || !std::isfinite(settings.Ambient.X) || !std::isfinite(settings.Ambient.Y)
        || !std::isfinite(settings.GustSpeed) || settings.GustSpeed < 0
        || (std::hypot(double(settings.Ambient.X), double(settings.Ambient.Y)) + std::sqrt(2.) * settings.GustSpeed) * settings.LatticeSeconds > .2
        || !std::isfinite(settings.Viscosity) || settings.Viscosity < 0 || settings.Viscosity > 100
        || !std::isfinite(settings.Relaxation) || settings.Relaxation < 0 || settings.Relaxation > 100)
        throw std::invalid_argument("invalid LBM settings or initial lattice speed > 0.2");
    // Open velocity/pressure boundaries amplify BGK's near-half relaxation
    // instability. Keep a dissipative floor and report its effective viscosity.
    Tau = std::max(settings.Boundary == WindBoundaryMode::Open ? .6 : .51, .5 + 3 * settings.Viscosity * settings.LatticeSeconds);
    Report.EffectiveViscosity = (Tau - .5) / (3 * settings.LatticeSeconds);
    F.Reset(Width, Height);
    Next.Reset(Width, Height);
    Solid.Reset(Width, Height);
    Velocity.Reset(Width, Height);
    std::mt19937 random(settings.Seed);
    const double phase = double(random()) / std::mt19937::max() * 2 * std::numbers::pi;
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const auto i = size_t(y) * Width + x;
            Solid[i] = world.MaterialAtIndex(i) != CellTypes::Empty;
            const WindVelocity v{settings.Ambient.X + settings.GustSpeed * float(std::sin(2 * std::numbers::pi * (y + .5) / Height + phase)),
                                 settings.Ambient.Y + settings.GustSpeed * float(std::sin(2 * std::numbers::pi * (x + .5) / Width - phase))};
            F[i] = Equilibrium(1, Solid[i] ? WindVelocity{} : v);
        }
    Measure();
}
LbmWindField::Distribution LbmWindField::Equilibrium(double rho, WindVelocity velocity) {
    double ux = velocity.X * Settings.LatticeSeconds, uy = velocity.Y * Settings.LatticeSeconds;
    const double speed = std::hypot(ux, uy);
    if (speed > .2) {
        ux *= .2 / speed;
        uy *= .2 / speed;
        ++Report.Stabilizations;
    }
    Distribution result{};
    for (size_t q = 0; q < 9; ++q) {
        const double cu = Cx[q] * ux + Cy[q] * uy;
        result[q] = Weight[q] * rho * (1 + 3 * cu + 4.5 * cu * cu - 1.5 * (ux * ux + uy * uy));
    }
    return result;
}
void LbmWindField::MoveDomain(WorldMapView world, uint32_t height, int64_t offsetX, int64_t offsetY) {
    LbmWindField next(world, height, Settings);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < world.Width; ++x) {
            const double oldX = double(x) + double(offsetX), oldY = double(y) + double(offsetY);
            const size_t i = size_t(y) * world.Width + x;
            if (oldX < 0 || oldY < 0 || oldX >= Width || oldY >= Height || next.Solid[i])
                continue;
            const size_t old = size_t(oldY) * Width + size_t(oldX);
            if (!Solid[old])
                next.F[i] = F[old];
        }
    next.Remainder = Remainder;
    next.Report.Stabilizations += Report.Stabilizations;
    next.Measure();
    *this = std::move(next);
}
void LbmWindField::ReconstructBoundary(uint32_t x, uint32_t y, int normalX, int normalY) {
    const size_t i = size_t(y) * Width + x;
    if (Solid[i])
        return;
    const int tangentX = -normalY, tangentY = normalX;
    auto direction = [&](int normal, int tangent) {
        const int vx = normal * normalX + tangent * tangentX;
        const int vy = normal * normalY + tangent * tangentY;
        for (size_t q = 0; q < 9; ++q)
            if (Cx[q] == vx && Cy[q] == vy)
                return q;
        return size_t(0);
    };
    auto &f = F[i];
    const size_t e = direction(1, 0), w = direction(-1, 0);
    const size_t n = direction(0, 1), s = direction(0, -1);
    const size_t ne = direction(1, 1), nw = direction(-1, 1);
    const size_t se = direction(1, -1), sw = direction(-1, -1);
    const double known = f[0] + f[n] + f[s] + 2 * (f[w] + f[nw] + f[sw]);
    const double ambientNormal = (Settings.Ambient.X * normalX + Settings.Ambient.Y * normalY) * Settings.LatticeSeconds;
    const auto side = normalX > 0 ? WindBoundarySide::Left : normalX < 0 ? WindBoundarySide::Right
                                                       : normalY > 0     ? WindBoundarySide::Top
                                                                         : WindBoundarySide::Bottom;
    const bool inflow = IsWindInflow(side, Settings.Ambient);
    double un = inflow ? ambientNormal : 1 - known;
    double rho = inflow ? known / (1 - un) : 1;
    // Prescribing the tangential background closes the pressure boundary;
    // extrapolating it from the previous step destabilizes side outlets.
    const auto v = Settings.Ambient;
    const double ut = (v.X * tangentX + v.Y * tangentY) * Settings.LatticeSeconds;
    // Zou and He, arXiv:comp-gas/9611001: nonequilibrium bounce-back
    // reconstructs the three unknown incoming
    // populations. Pressure outlets fix rho=1 and infer their normal velocity.
    f[e] = f[w] + (2. / 3) * rho * un;
    f[ne] = f[sw] + .5 * (f[s] - f[n]) + rho * un / 6 + .5 * rho * ut;
    f[se] = f[nw] + .5 * (f[n] - f[s]) + rho * un / 6 - .5 * rho * ut;
    if (!std::isfinite(rho) || rho <= 0 || std::abs(un) > .2
        || std::any_of(f.begin(), f.end(), [](double value) { return !std::isfinite(value) || value < 0; })) {
        f = Equilibrium(1, Settings.Ambient);
        ++Report.Stabilizations;
    }
}
void LbmWindField::ApplyOpenBoundaries() {
    if (Width < 2 || Height < 2) {
        for (size_t i = 0; i < F.size(); ++i)
            if (!Solid[i])
                F[i] = Equilibrium(1, Settings.Ambient);
        return;
    }
    for (uint32_t y = 1; y + 1 < Height; ++y) {
        ReconstructBoundary(0, y, 1, 0);
        ReconstructBoundary(Width - 1, y, -1, 0);
    }
    for (uint32_t x = 1; x + 1 < Width; ++x) {
        ReconstructBoundary(x, 0, 0, 1);
        ReconstructBoundary(x, Height - 1, 0, -1);
    }
    // Two intersecting Zou-He equations leave corner diagonals underdetermined.
    // Close corners with equilibrium at the adjacent interior density. This is
    // explicit and does not let sequential edge reconstruction reuse stale f.
    for (uint32_t y : {0u, Height - 1})
        for (uint32_t x : {0u, Width - 1}) {
            const size_t i = size_t(y) * Width + x;
            if (Solid[i])
                continue;
            const uint32_t ix = x == 0 ? 1 : Width - 2, iy = y == 0 ? 1 : Height - 2;
            const size_t inner = size_t(iy) * Width + ix;
            const double rho = std::accumulate(F[inner].begin(), F[inner].end(), 0.);
            const bool inletX = IsWindInflow(x == 0 ? WindBoundarySide::Left : WindBoundarySide::Right, Settings.Ambient);
            const bool inletY = IsWindInflow(y == 0 ? WindBoundarySide::Top : WindBoundarySide::Bottom, Settings.Ambient);
            const auto velocity = inletX || inletY ? Settings.Ambient : Velocity[inner];
            F[i] = Equilibrium(std::isfinite(rho) && rho > 0 ? rho : 1, velocity);
        }
}
void LbmWindField::SetObstacles(WorldMapView world) {
    if (!world.IsValid() || world.Width != Width || world.Height < Height || world.CellCount() != size_t(world.Width) * world.Height)
        throw std::invalid_argument("LBM obstacle extent mismatch");
    bool changed = false;
    for (size_t linear = 0; linear < Solid.size(); ++linear) {
        const auto i = Solid.Address(uint32_t(linear % Width), uint32_t(linear / Width));
        const bool solid = world.MaterialAtIndex(linear) != CellTypes::Empty;
        if (Solid[i] != solid) {
            Solid[i] = solid;
            F[i] = Equilibrium(1, {});
            changed = true;
        }
    }
    if (changed)
        Measure(); // Moving walls displace/reset air; no global mass claim here.
}
void LbmWindField::Measure() {
    Report.MinDensity = std::numeric_limits<double>::infinity();
    Report.MaxDensity = Report.Mass = 0;
    for (size_t linear = 0; linear < F.size(); ++linear) {
        const auto i = F.Address(uint32_t(linear % Width), uint32_t(linear / Width));
        Velocity[i] = {};
        if (Solid[i])
            continue;
        const double rho = std::accumulate(F[i].begin(), F[i].end(), 0.);
        if (!std::isfinite(rho) || rho <= 0)
            throw std::runtime_error("LBM density became nonpositive or nonfinite");
        double ux = 0, uy = 0;
        for (size_t q = 0; q < 9; ++q) {
            ux += Cx[q] * F[i][q];
            uy += Cy[q] * F[i][q];
        }
        Velocity[i] = {float(ux / (rho * Settings.LatticeSeconds)), float(uy / (rho * Settings.LatticeSeconds))};
        Report.MinDensity = std::min(Report.MinDensity, rho);
        Report.MaxDensity = std::max(Report.MaxDensity, rho);
        Report.Mass += rho;
    }
    if (!std::isfinite(Report.MinDensity))
        Report.MinDensity = 0;
}
void LbmWindField::CollideAndStream() {
    Next.Fill(Distribution{});
    const float blend = float(-std::expm1(-Settings.Relaxation * Settings.LatticeSeconds));
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const auto i = F.Address(x, y);
            if (Solid[i])
                continue;
            const double rho = std::accumulate(F[i].begin(), F[i].end(), 0.);
            const auto v = Velocity[i];
            const auto equilibrium = Equilibrium(rho, v);
            const WindVelocity target{std::lerp(v.X, Settings.Ambient.X, blend), std::lerp(v.Y, Settings.Ambient.Y, blend)};
            const auto forced = Equilibrium(rho, target);
            Distribution post{};
            bool positive = true;
            for (size_t q = 0; q < 9; ++q) {
                post[q] = F[i][q] + (equilibrium[q] - F[i][q]) / Tau + forced[q] - equilibrium[q];
                positive &= std::isfinite(post[q]) && post[q] >= 0;
            }
            if (!positive) {
                post = forced;
                ++Report.Stabilizations;
            }
            for (size_t q = 0; q < 9; ++q) {
                int nx = int(x) + Cx[q], ny = int(y) + Cy[q];
                if (Settings.Boundary == WindBoundaryMode::Periodic) {
                    nx = (nx + int(Width)) % int(Width);
                    ny = (ny + int(Height)) % int(Height);
                } else if (nx < 0 || ny < 0 || nx >= int(Width) || ny >= int(Height)) {
                    continue; // Outgoing populations leave; unknown incoming populations follow below.
                }
                const auto j = F.Address(uint32_t(nx), uint32_t(ny));
                if (Solid[j])
                    Next[i][Opposite[q]] = post[q];
                else
                    Next[j][q] = post[q];
            }
        }
    F.swap(Next);
    if (Settings.Boundary == WindBoundaryMode::Open)
        ApplyOpenBoundaries();
    Measure();
}
void LbmWindField::Step(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    Remainder += std::min(double(seconds), .25);
    Report.Substeps = 0;
    while (Remainder + 1e-12 >= Settings.LatticeSeconds) {
        CollideAndStream();
        Remainder -= Settings.LatticeSeconds;
        ++Report.Substeps;
    }
}
bool LbmWindField::IsSolid(uint32_t x, uint32_t y) const { return x < Width && y < Height && Solid[size_t(y) * Width + x]; }
WindVelocity LbmWindField::Sample(float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height)
        return {};
    return Velocity[size_t(y) * Width + uint32_t(x)];
}
void LbmWindField::AddImpulse(float x, float y, float radius, WindVelocity delta) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height || !std::isfinite(radius) || radius <= 0
        || !std::isfinite(delta.X) || !std::isfinite(delta.Y) || std::abs(delta.X) > 1000 || std::abs(delta.Y) > 1000)
        throw std::invalid_argument("invalid LBM wind impulse");
    for (uint32_t py = 0; py < Height; ++py)
        for (uint32_t px = 0; px < Width; ++px) {
            const auto i = size_t(py) * Width + px;
            if (Solid[i])
                continue;
            const double dx = WindDistance(px + .5 - x, double(Width), Settings.Boundary) / radius;
            const double dy = WindDistance(py + .5 - y, double(Height), Settings.Boundary) / radius;
            const float weight = float(std::pow(std::max(0., 1 - dx * dx - dy * dy), 2));
            if (weight == 0 || (delta.X == 0 && delta.Y == 0))
                continue;
            const double rho = std::accumulate(F[i].begin(), F[i].end(), 0.);
            F[i] = Equilibrium(rho, {Velocity[i].X + delta.X * weight, Velocity[i].Y + delta.Y * weight});
        }
    Measure();
}
} // namespace pixel
