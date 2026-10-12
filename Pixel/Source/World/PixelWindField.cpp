#include "PixelWindField.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>

namespace pixel {
namespace {
bool Bounded(float value, float limit) { return std::isfinite(value) && std::abs(value) <= limit; }
double Dot(const std::vector<double> &a, const std::vector<double> &b) {
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += a[i] * b[i];
    return sum;
}
} // namespace
size_t PixelWindField::Index(int x, int y) const {
    if (x < 0)
        x += int(Width);
    else if (x >= int(Width))
        x -= int(Width);
    if (y < 0)
        y += int(Height);
    else if (y >= int(Height))
        y -= int(Height);
    return size_t(y) * Width + size_t(x);
}
size_t PixelWindField::CheckedIndex(uint32_t x, uint32_t y) const {
    if (x >= Width || y >= Height)
        throw std::out_of_range("wind face outside grid");
    return size_t(y) * Width + x;
}
double PixelWindField::FaceX(uint32_t x, uint32_t y) const { return U.at(CheckedIndex(x, y)); }
double PixelWindField::FaceY(uint32_t x, uint32_t y) const { return V.at(CheckedIndex(x, y)); }
bool PixelWindField::IsSolid(uint32_t x, uint32_t y) const { return Solid.at(CheckedIndex(x, y)) != 0; }
void PixelWindField::Reset(uint32_t width, uint32_t endRow, const PixelWindSettings &settings) {
    if (width < 1 || endRow < 1 || width > 2048 || endRow > 2048 || !Bounded(settings.Ambient.X, 1000) || !Bounded(settings.Ambient.Y, 1000) || !Bounded(settings.GustSpeed, 1000) || settings.GustSpeed < 0 || !Bounded(settings.Viscosity, 100) || settings.Viscosity < 0 || !Bounded(settings.Relaxation, 100) || settings.Relaxation < 0 || !std::isfinite(settings.DivergenceTolerance) || settings.DivergenceTolerance < 1e-10 || settings.DivergenceTolerance > 0.1 || !settings.MaxPressureIterations || settings.MaxPressureIterations > 20000)
        throw std::invalid_argument("invalid pixel wind settings (dimensions 1..2048)");
    PixelWindField next;
    next.Width = width;
    next.Height = endRow;
    next.Settings = settings;
    const size_t count = size_t(width) * endRow;
    for (auto *values : {&next.U, &next.V, &next.NextU, &next.NextV, &next.Pressure, &next.Residual,
                         &next.Direction, &next.Product, &next.Preconditioned, &next.Diagonal})
        values->resize(count);
    next.Solid.resize(count);
    next.OpenX.resize(count);
    next.OpenY.resize(count);
    next.SupportX.resize(count);
    next.SupportY.resize(count);
    std::mt19937 random(settings.Seed);
    const double phase = double(random()) / std::mt19937::max() * 2 * std::numbers::pi;
    for (uint32_t y = 0; y < endRow; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            next.U[i] = settings.Ambient.X + settings.GustSpeed * std::sin(2 * std::numbers::pi * (y + 0.5) / endRow + phase);
            next.V[i] = settings.Ambient.Y + settings.GustSpeed * std::sin(2 * std::numbers::pi * (x + 0.5) / width - phase);
        }
    next.RebuildFaces();
    *this = std::move(next);
}
void PixelWindField::RebuildFaces() {
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const size_t i = Index(x, y);
            SupportX[i] = !Solid[i] || !Solid[Index(int(x) - 1, y)];
            SupportY[i] = !Solid[i] || !Solid[Index(x, int(y) - 1)];
            OpenX[i] = !Solid[i] && !Solid[Index(int(x) - 1, y)];
            OpenY[i] = !Solid[i] && !Solid[Index(x, int(y) - 1)];
            if (!OpenX[i])
                U[i] = 0;
            if (!OpenY[i])
                V[i] = 0;
        }
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const size_t i = Index(x, y);
            Diagonal[i] = OpenX[i] + OpenX[Index(x + 1, y)] + OpenY[i] + OpenY[Index(x, y + 1)];
        }
}
void PixelWindField::SetObstacles(WorldStateView world) {
    if (!IsEnabled() || world.Width != Width || world.Height < Height || world.Cells.size() != size_t(world.Width) * world.Height)
        throw std::invalid_argument("obstacle world does not match pixel wind");
    bool changed = false;
    for (size_t i = 0; i < Solid.size(); ++i) {
        const bool solid = world.Cells[i].Type != CellTypes::Empty;
        changed |= Solid[i] != solid;
        Solid[i] = solid;
    }
    if (!changed)
        return;
    RebuildFaces();
    Project();
}
double PixelWindField::SampleFace(const std::vector<double> &values, const std::vector<uint8_t> &open, double x, double y) const {
    x = std::fmod(x, double(Width));
    y = std::fmod(y, double(Height));
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    double sum = 0, weights = 0;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const size_t i = Index(ix + dx, iy + dy);
            const double weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            if (open[i]) {
                sum += weight * values[i];
                weights += weight;
            }
        }
    return weights > 0 ? sum / weights : 0;
}
WindVelocity PixelWindField::Sample(float x, float y) const {
    if (!IsEnabled() || !std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height || Solid[Index(int(x), int(y))])
        return {};
    return {float(SampleFace(U, SupportX, x, y - 0.5)), float(SampleFace(V, SupportY, x - 0.5, y))};
}
void PixelWindField::Transport(double dt) {
    // Explicit diffusion first (dt*nu <= 0.2), then advection of that result.
    // Mixing an old-state Laplacian with an advected value is not monotone.
    auto diffuse = [&](const auto &values, const auto &open, auto &next, bool horizontal) {
        for (uint32_t y = 0; y < Height; ++y)
            for (uint32_t x = 0; x < Width; ++x) {
                const size_t i = Index(x, y);
                double laplacian = 0;
                for (const auto j : {Index(int(x) - 1, y), Index(x + 1, y)})
                    if (horizontal || open[j])
                        laplacian += values[j] - values[i];
                for (const auto j : {Index(x, int(y) - 1), Index(x, y + 1)})
                    if (!horizontal || open[j])
                        laplacian += values[j] - values[i];
                next[i] = open[i] ? values[i] + Settings.Viscosity * dt * laplacian : 0;
            }
    };
    diffuse(U, OpenX, NextU, true);
    diffuse(V, OpenY, NextV, false);
    U.swap(NextU);
    V.swap(NextV);
    auto component = [&](const std::vector<double> &values, const std::vector<uint8_t> &open, const std::vector<uint8_t> &support,
                         std::vector<double> &next, double offsetX, double offsetY, double ambient) {
        for (uint32_t y = 0; y < Height; ++y)
            for (uint32_t x = 0; x < Width; ++x) {
                const size_t i = Index(x, y);
                if (!open[i]) {
                    next[i] = 0;
                    continue;
                }
                const double px = x + offsetX, py = y + offsetY;
                const double ux = SampleFace(U, SupportX, px, py - 0.5);
                const double vy = SampleFace(V, SupportY, px - 0.5, py);
                double bx = px - dt * ux, by = py - dt * vy;
                // With <=0.5 pixel travel per axis, the midpoint/end checks
                // prevent the trace from entering or crossing a one-pixel wall.
                if (Solid[Index(int(std::floor(bx)), int(std::floor(by)))] || Solid[Index(int(std::floor((px + bx) * 0.5)), int(std::floor((py + by) * 0.5)))]) {
                    bx = px;
                    by = py;
                }
                const double advected = SampleFace(values, support, bx - offsetX, by - offsetY);
                next[i] = ambient + (advected - ambient) * std::exp(-Settings.Relaxation * dt);
            }
    };
    component(U, OpenX, SupportX, NextU, 0, 0.5, Settings.Ambient.X);
    component(V, OpenY, SupportY, NextV, 0.5, 0, Settings.Ambient.Y);
    U.swap(NextU);
    V.swap(NextV);
}
void PixelWindField::ApplyMatrix(const std::vector<double> &input, std::vector<double> &output) const {
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const size_t i = Index(x, y), left = Index(int(x) - 1, y), right = Index(x + 1, y);
            const size_t up = Index(x, int(y) - 1), down = Index(x, y + 1);
            output[i] = Diagonal[i] * input[i] - OpenX[i] * input[left] - OpenX[right] * input[right]
                      - OpenY[i] * input[up] - OpenY[down] * input[down];
        }
}
void PixelWindField::Project() {
    std::fill(Pressure.begin(), Pressure.end(), 0);
    size_t fluidCount = 0;
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const size_t i = Index(x, y);
            Residual[i] = U[i] - U[Index(x + 1, y)] + V[i] - V[Index(x, y + 1)];
            Preconditioned[i] = Diagonal[i] ? Residual[i] / Diagonal[i] : 0;
            fluidCount += !Solid[i];
        }
    Direction = Preconditioned;
    double rz = Dot(Residual, Preconditioned);
    const double target = Settings.DivergenceTolerance * Settings.DivergenceTolerance * std::max(size_t(1), fluidCount);
    uint32_t iterations = 0;
    while (Dot(Residual, Residual) > target && iterations < Settings.MaxPressureIterations) {
        ApplyMatrix(Direction, Product);
        const double denominator = Dot(Direction, Product);
        if (denominator <= 0 || !std::isfinite(denominator))
            break;
        const double alpha = rz / denominator;
        for (size_t i = 0; i < Pressure.size(); ++i) {
            Pressure[i] += alpha * Direction[i];
            Residual[i] -= alpha * Product[i];
            Preconditioned[i] = Diagonal[i] ? Residual[i] / Diagonal[i] : 0;
        }
        const double nextRz = Dot(Residual, Preconditioned);
        const double beta = rz > 0 ? nextRz / rz : 0;
        for (size_t i = 0; i < Direction.size(); ++i)
            Direction[i] = Preconditioned[i] + beta * Direction[i];
        rz = nextRz;
        ++iterations;
    }
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const size_t i = Index(x, y);
            U[i] -= OpenX[i] * (Pressure[i] - Pressure[Index(int(x) - 1, y)]);
            V[i] -= OpenY[i] * (Pressure[i] - Pressure[Index(x, int(y) - 1)]);
        }
    Report.PressureIterations += iterations;
    MeasureDivergence();
    Report.Converged = Report.Converged && Report.RmsDivergence <= Settings.DivergenceTolerance * 1.01;
}
void PixelWindField::MeasureDivergence() {
    double sum = 0, maximum = 0;
    size_t count = 0;
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const size_t i = Index(x, y);
            if (Solid[i])
                continue;
            const double divergence = U[Index(x + 1, y)] - U[i] + V[Index(x, y + 1)] - V[i];
            sum += divergence * divergence;
            maximum = std::max(maximum, std::abs(divergence));
            ++count;
        }
    Report.RmsDivergence = std::sqrt(sum / std::max(size_t(1), count));
    Report.MaxDivergence = maximum;
}
void PixelWindField::Step(float seconds) {
    if (!IsEnabled() || !std::isfinite(seconds) || seconds <= 0)
        return;
    Report = {};
    double remaining = std::min(double(seconds), 0.25);
    while (remaining > 1e-12) {
        double speed = std::max(std::abs(double(Settings.Ambient.X)), std::abs(double(Settings.Ambient.Y)));
        for (size_t i = 0; i < U.size(); ++i)
            speed = std::max({speed, std::abs(U[i]), std::abs(V[i])});
        const double dt = std::min({remaining, 0.5 / std::max(1.0, speed), 0.2 / std::max(1.0, double(Settings.Viscosity))});
        Transport(dt);
        Project();
        remaining -= dt;
        ++Report.Substeps;
    }
}
void PixelWindField::SetFaceVelocities(std::span<const double> u, std::span<const double> v) {
    if (!IsEnabled() || u.size() != U.size() || v.size() != V.size())
        throw std::invalid_argument("face velocity array does not match grid");
    for (size_t i = 0; i < u.size(); ++i)
        if (!std::isfinite(u[i]) || !std::isfinite(v[i]) || std::abs(u[i]) > 2000 || std::abs(v[i]) > 2000)
            throw std::invalid_argument("initial face velocity must be finite and within 2000 pixels/s");
    std::copy(u.begin(), u.end(), U.begin());
    std::copy(v.begin(), v.end(), V.begin());
    RebuildFaces();
    Report = {};
    Project();
}
void PixelWindField::AddImpulse(float x, float y, float radius, WindVelocity delta) {
    if (!IsEnabled() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) || radius <= 0 || !Bounded(delta.X, 1000) || !Bounded(delta.Y, 1000))
        throw std::invalid_argument("invalid pixel wind impulse");
    auto distance = [](double a, double b, double extent) { return std::remainder(a - b, extent); };
    for (uint32_t row = 0; row < Height; ++row)
        for (uint32_t col = 0; col < Width; ++col) {
            const size_t i = Index(col, row);
            auto weight = [&](double px, double py) {
                const double dx = distance(px, x, Width), dy = distance(py, y, Height);
                const double r = std::sqrt(dx * dx + dy * dy) / radius;
                return r < 1 ? 0.5 * (1 + std::cos(std::numbers::pi * r)) : 0;
            };
            const double dx = delta.X * weight(col, row + 0.5), dy = delta.Y * weight(col + 0.5, row);
            if (OpenX[i] && dx != 0)
                U[i] = std::clamp(U[i] + dx, -1000.0, 1000.0);
            if (OpenY[i] && dy != 0)
                V[i] = std::clamp(V[i] + dy, -1000.0, 1000.0);
        }
    Report = {};
    Project();
}
} // namespace pixel
