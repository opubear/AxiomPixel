#include "LbmWindField.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <random>
#include <stdexcept>

namespace pixel {
namespace {
constexpr std::array<int, 9> Cx{0, 1, 0, -1, 0, 1, -1, -1, 1};
constexpr std::array<int, 9> Cy{0, 0, 1, 0, -1, 1, 1, -1, -1};
constexpr std::array<int, 9> Opposite{0, 3, 4, 1, 2, 7, 8, 5, 6};
constexpr std::array<double, 9> Weight{4. / 9, 1. / 9, 1. / 9, 1. / 9, 1. / 9, 1. / 36, 1. / 36, 1. / 36, 1. / 36};
} // namespace
LbmWindField::LbmWindField(WorldStateView world, uint32_t height, const LbmWindSettings &settings)
    : Width(world.Width), Height(height), Settings(settings) {
    if (!Width || !Height || Width > 2048 || Height > 2048 || world.Height < Height || world.Cells.size() != size_t(Width) * world.Height
        || !std::isfinite(settings.LatticeSeconds) || settings.LatticeSeconds < 1.0 / 4000 || settings.LatticeSeconds > 1.0 / 60
        || !std::isfinite(settings.Ambient.X) || !std::isfinite(settings.Ambient.Y)
        || !std::isfinite(settings.GustSpeed) || settings.GustSpeed < 0
        || (std::hypot(double(settings.Ambient.X), double(settings.Ambient.Y)) + std::sqrt(2.) * settings.GustSpeed) * settings.LatticeSeconds > .2
        || !std::isfinite(settings.Viscosity) || settings.Viscosity < 0 || settings.Viscosity > 100
        || !std::isfinite(settings.Relaxation) || settings.Relaxation < 0 || settings.Relaxation > 100)
        throw std::invalid_argument("invalid LBM settings or initial lattice speed > 0.2");
    Tau = std::max(.51, .5 + 3 * settings.Viscosity * settings.LatticeSeconds);
    Report.EffectiveViscosity = (Tau - .5) / (3 * settings.LatticeSeconds);
    F.resize(size_t(Width) * Height);
    Next.resize(F.size());
    Solid.resize(F.size());
    Velocity.resize(F.size());
    std::mt19937 random(settings.Seed);
    const double phase = double(random()) / std::mt19937::max() * 2 * std::numbers::pi;
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const auto i = size_t(y) * Width + x;
            Solid[i] = world.Cells[i].Type != CellTypes::Empty;
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
void LbmWindField::SetObstacles(WorldStateView world) {
    if (world.Width != Width || world.Height < Height || world.Cells.size() != size_t(world.Width) * world.Height)
        throw std::invalid_argument("LBM obstacle extent mismatch");
    bool changed = false;
    for (size_t i = 0; i < Solid.size(); ++i) {
        const bool solid = world.Cells[i].Type != CellTypes::Empty;
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
    for (size_t i = 0; i < F.size(); ++i) {
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
    const float blend = float(-std::expm1(-Settings.Relaxation * Settings.LatticeSeconds));
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const auto i = size_t(y) * Width + x;
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
                const auto nx = uint32_t((int(x) + Cx[q] + int(Width)) % int(Width));
                const auto ny = uint32_t((int(y) + Cy[q] + int(Height)) % int(Height));
                const auto j = size_t(ny) * Width + nx;
                if (Solid[j])
                    Next[i][Opposite[q]] = post[q];
                else
                    Next[j][q] = post[q];
            }
        }
    F.swap(Next);
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
            const double dx = std::remainder(px + .5 - x, double(Width)) / radius;
            const double dy = std::remainder(py + .5 - y, double(Height)) / radius;
            const float weight = float(std::pow(std::max(0., 1 - dx * dx - dy * dy), 2));
            if (weight == 0 || (delta.X == 0 && delta.Y == 0))
                continue;
            const double rho = std::accumulate(F[i].begin(), F[i].end(), 0.);
            F[i] = Equilibrium(rho, {Velocity[i].X + delta.X * weight, Velocity[i].Y + delta.Y * weight});
        }
    Measure();
}
} // namespace pixel
