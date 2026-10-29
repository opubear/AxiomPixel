#include "PixelWindField.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <stdexcept>

namespace pixel {
performance::MemoryUsage PixelWindField::GetMemoryUsage() const {
    return performance::MeasureObject(*this, U, V, NextU, NextV, Solid, OpenX, OpenY, SupportX, SupportY, Pressure, Residual, Direction, Product, Preconditioned, Diagonal, RightU, BottomV);
}

namespace {
bool Bounded(float value, float limit) { return std::isfinite(value) && std::abs(value) <= limit; }
double Dot(const memory::ChunkedGrid<double> &a, const memory::ChunkedGrid<double> &b) {
    double sum = 0;
    memory::ForEachGridRow([&](auto left, auto right) {
        for (size_t i = 0; i < left.size(); ++i) sum += left[i] * right[i];
    }, a, b);
    return sum;
}
} // namespace
memory::GridAddress PixelWindField::Index(int x, int y) const {
    if (Periodic()) {
        x = (x % int(Width) + int(Width)) % int(Width);
        y = (y % int(Height) + int(Height)) % int(Height);
    } else {
        x = std::clamp(x, 0, int(Width) - 1);
        y = std::clamp(y, 0, int(Height) - 1);
    }
    return U.Address(uint32_t(x), uint32_t(y));
}

double PixelWindField::UFace(int x, int y) const {
    if (!Periodic() && x >= int(Width))
        return RightU[size_t(std::clamp(y, 0, int(Height) - 1))];
    return U[Index(x, y)];
}
double PixelWindField::VFace(int x, int y) const {
    if (!Periodic() && y >= int(Height))
        return BottomV[size_t(std::clamp(x, 0, int(Width) - 1))];
    return V[Index(x, y)];
}
double PixelWindField::DivergenceAt(int x, int y) const {
    return UFace(x + 1, y) - UFace(x, y) + VFace(x, y + 1) - VFace(x, y);
}
double PixelWindField::PressureWeight(int x, int y, int dx, int dy) const {
    if (Solid[Index(x, y)])
        return 0;
    const int nx = x + dx, ny = y + dy;
    if (Periodic() || (nx >= 0 && ny >= 0 && nx < int(Width) && ny < int(Height)))
        return !Solid[Index(nx, ny)];
    const auto side = nx < 0 ? WindBoundarySide::Left : nx >= int(Width) ? WindBoundarySide::Right
                                                  : ny < 0               ? WindBoundarySide::Top
                                                                         : WindBoundarySide::Bottom;
    return Inflow(side) ? 0 : 1; // Prescribed flux in; zero exterior pressure out.
}
void PixelWindField::PrescribeInflow() {
    if (Periodic())
        return;
    for (uint32_t y = 0; y < Height; ++y) {
        if (Inflow(WindBoundarySide::Left))
            U[Index(0, y)] = Solid[Index(0, y)] ? 0 : Settings.Ambient.X;
        if (Inflow(WindBoundarySide::Right))
            RightU[y] = Solid[Index(Width - 1, y)] ? 0 : Settings.Ambient.X;
    }
    for (uint32_t x = 0; x < Width; ++x) {
        if (Inflow(WindBoundarySide::Top))
            V[Index(x, 0)] = Solid[Index(x, 0)] ? 0 : Settings.Ambient.Y;
        if (Inflow(WindBoundarySide::Bottom))
            BottomV[x] = Solid[Index(x, Height - 1)] ? 0 : Settings.Ambient.Y;
    }
}
memory::GridAddress PixelWindField::CheckedIndex(uint32_t x, uint32_t y) const {
    if (x >= Width || y >= Height)
        throw std::out_of_range("wind face outside grid");
    return U.Address(x, y);
}
double PixelWindField::FaceX(uint32_t x, uint32_t y) const {
    if (!Periodic() && x == Width && y < Height)
        return RightU.at(y);
    return U.at(CheckedIndex(x, y));
}
double PixelWindField::FaceY(uint32_t x, uint32_t y) const {
    if (!Periodic() && y == Height && x < Width)
        return BottomV.at(x);
    return V.at(CheckedIndex(x, y));
}
bool PixelWindField::IsSolid(uint32_t x, uint32_t y) const { return Solid.at(CheckedIndex(x, y)) != 0; }
void PixelWindField::Reset(uint32_t width, uint32_t endRow, const PixelWindSettings &settings) {
    if (!IsValidWindBoundary(settings.Boundary) || width < 1 || endRow < 1 || width > 2048 || endRow > 2048 || !Bounded(settings.Ambient.X, 1000) || !Bounded(settings.Ambient.Y, 1000) || !Bounded(settings.GustSpeed, 1000) || settings.GustSpeed < 0 || !Bounded(settings.Viscosity, 100) || settings.Viscosity < 0 || !Bounded(settings.Relaxation, 100) || settings.Relaxation < 0 || !std::isfinite(settings.DivergenceTolerance) || settings.DivergenceTolerance < 1e-10 || settings.DivergenceTolerance > 0.1 || !settings.MaxPressureIterations || settings.MaxPressureIterations > 20000)
        throw std::invalid_argument("invalid pixel wind settings (dimensions 1..2048)");
    PixelWindField next;
    next.Width = width;
    next.Height = endRow;
    next.Settings = settings;
    for (auto *values : {&next.U, &next.V, &next.NextU, &next.NextV, &next.Pressure, &next.Residual,
                         &next.Direction, &next.Product, &next.Preconditioned, &next.Diagonal})
        values->Reset(width, endRow);
    next.RightU.Reset(1, endRow, settings.Ambient.X);
    next.BottomV.Reset(width, 1, settings.Ambient.Y);
    next.Solid.Reset(width, endRow);
    next.OpenX.Reset(width, endRow);
    next.OpenY.Reset(width, endRow);
    next.SupportX.Reset(width, endRow);
    next.SupportY.Reset(width, endRow);
    std::mt19937 random(settings.Seed);
    const double phase = double(random()) / std::mt19937::max() * 2 * std::numbers::pi;
    for (uint32_t y = 0; y < endRow; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            next.U[i] = settings.Ambient.X + settings.GustSpeed * std::sin(2 * std::numbers::pi * (y + 0.5) / endRow + phase);
            next.V[i] = settings.Ambient.Y + settings.GustSpeed * std::sin(2 * std::numbers::pi * (x + 0.5) / width - phase);
        }
    next.RebuildFaces();
    if (!next.Periodic())
        next.Project();
    *this = std::move(next);
}
void PixelWindField::RebuildFaces() {
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const auto i = Index(x, y);
            const bool left = !Periodic() && x == 0 ? false : Solid[Index(int(x) - 1, y)];
            const bool up = !Periodic() && y == 0 ? false : Solid[Index(x, int(y) - 1)];
            SupportX[i] = !Solid[i] || !left;
            SupportY[i] = !Solid[i] || !up;
            OpenX[i] = !Solid[i] && !left;
            OpenY[i] = !Solid[i] && !up;
            if (!OpenX[i])
                U[i] = 0;
            if (!OpenY[i])
                V[i] = 0;
            Diagonal[i] = PressureWeight(x, y, -1, 0) + PressureWeight(x, y, 1, 0)
                        + PressureWeight(x, y, 0, -1) + PressureWeight(x, y, 0, 1);
        }
    for (uint32_t y = 0; y < Height; ++y)
        if (Solid[Index(Width - 1, y)])
            RightU[y] = 0;
    for (uint32_t x = 0; x < Width; ++x)
        if (Solid[Index(x, Height - 1)])
            BottomV[x] = 0;
    PrescribeInflow();
}
void PixelWindField::MoveDomain(WorldMapView world, uint32_t height, int64_t offsetX, int64_t offsetY) {
    PixelWindField next;
    next.Reset(world.Width, height, Settings);
    next.SetObstacles(world);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < world.Width; ++x) {
            const double oldX = double(x) + double(offsetX), oldY = double(y) + double(offsetY);
            if (oldX < 0 || oldY < 0 || oldX >= Width || oldY >= Height)
                continue;
            const auto i = next.Index(x, y);
            next.U[i] = SampleFace(U, SupportX, oldX, oldY);
            next.V[i] = SampleFace(V, SupportY, oldX, oldY);
        }
    for (uint32_t y = 0; y < height; ++y) {
        const double oldX = double(world.Width) + double(offsetX), oldY = double(y) + double(offsetY);
        if (oldX >= 0 && oldX <= Width && oldY >= 0 && oldY < Height)
            next.RightU[y] = SampleFace(U, SupportX, oldX, oldY);
    }
    for (uint32_t x = 0; x < world.Width; ++x) {
        const double oldX = double(x) + double(offsetX), oldY = double(height) + double(offsetY);
        if (oldX >= 0 && oldX < Width && oldY >= 0 && oldY <= Height)
            next.BottomV[x] = SampleFace(V, SupportY, oldX, oldY);
    }
    next.RebuildFaces();
    next.Project();
    *this = std::move(next);
}
void PixelWindField::SetObstacles(WorldMapView world) {
    if (!IsEnabled() || !world.IsValid() || world.Width != Width || world.Height < Height || world.CellCount() != size_t(world.Width) * world.Height)
        throw std::invalid_argument("obstacle world does not match pixel wind");
    bool changed = false;
    for (size_t i = 0; i < Solid.size(); ++i) {
        const bool solid = world.MaterialAtIndex(i) != CellTypes::Empty;
        changed |= Solid[i] != solid;
        Solid[i] = solid;
    }
    if (!changed)
        return;
    RebuildFaces();
    Project();
}
double PixelWindField::SampleFace(const memory::ChunkedGrid<double> &values, const memory::ChunkedGrid<uint8_t> &open, double x, double y) const {
    const bool horizontal = &values == &U || &values == &NextU;
    if (Periodic()) {
        x = std::fmod(x, double(Width));
        y = std::fmod(y, double(Height));
    } else {
        const double px = x + (horizontal ? 0 : .5), py = y + (horizontal ? .5 : 0);
        if ((px < 0 && Inflow(WindBoundarySide::Left)) || (px > Width && Inflow(WindBoundarySide::Right))
            || (py < 0 && Inflow(WindBoundarySide::Top)) || (py > Height && Inflow(WindBoundarySide::Bottom)))
            return horizontal ? Settings.Ambient.X : Settings.Ambient.Y;
        x = std::clamp(x, 0., double(horizontal ? Width : Width - 1));
        y = std::clamp(y, 0., double(horizontal ? Height - 1 : Height));
    }
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    double sum = 0, weights = 0;
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            const int px = ix + dx, py = iy + dy;
            const auto i = Index(px, py);
            const double weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
            const bool outerX = !Periodic() && horizontal && px >= int(Width);
            const bool outerY = !Periodic() && !horizontal && py >= int(Height);
            if (outerX || outerY || open[i]) {
                const double value = outerX ? UFace(Width, py) : outerY ? VFace(px, Height)
                                                                        : values[i];
                sum += weight * value;
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
                const auto i = Index(x, y);
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
    auto component = [&](const memory::ChunkedGrid<double> &values, const memory::ChunkedGrid<uint8_t> &open, const memory::ChunkedGrid<uint8_t> &support,
                         memory::ChunkedGrid<double> &next, double offsetX, double offsetY, double ambient) {
        for (uint32_t y = 0; y < Height; ++y)
            for (uint32_t x = 0; x < Width; ++x) {
                const auto i = Index(x, y);
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
                if ((Periodic() || (bx >= 0 && by >= 0 && bx < Width && by < Height))
                    && (Solid[Index(int(std::floor(bx)), int(std::floor(by)))] || Solid[Index(int(std::floor((px + bx) * 0.5)), int(std::floor((py + by) * 0.5)))])) {
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
    if (!Periodic()) {
        for (uint32_t y = 0; y < Height; ++y)
            if (!Inflow(WindBoundarySide::Right))
                RightU[y] = U[Index(Width - 1, y)];
        for (uint32_t x = 0; x < Width; ++x)
            if (!Inflow(WindBoundarySide::Bottom))
                BottomV[x] = V[Index(x, Height - 1)];
    }
}
void PixelWindField::ApplyMatrix(const memory::ChunkedGrid<double> &input, memory::ChunkedGrid<double> &output) const {
    const auto boundary = [&](int x, int y) {
            const auto i = Index(x, y);
            output[i] = Diagonal[i] * input[i];
            for (const auto [dx, dy] : {std::pair{-1, 0}, {1, 0}, {0, -1}, {0, 1}})
                if (Periodic() || (x + dx >= 0 && y + dy >= 0 && x + dx < int(Width) && y + dy < int(Height)))
                    output[i] -= PressureWeight(x, y, dx, dy) * input[Index(x + dx, y + dy)];
    };
    for (uint32_t y = 0; y < Height; ++y) {
        if (y == 0 || y + 1 == Height) {
            for (uint32_t x = 0; x < Width; ++x) boundary(x, y);
            continue;
        }
        for (uint32_t x = 0; x < Width; x += input.TileSize) {
            const auto center = input.RowSegment(x, y), above = input.RowSegment(x, y - 1), below = input.RowSegment(x, y + 1);
            const auto solid = Solid.RowSegment(x, y), solidAbove = Solid.RowSegment(x, y - 1), solidBelow = Solid.RowSegment(x, y + 1);
            const auto diagonal = Diagonal.RowSegment(x, y);
            auto result = output.RowSegment(x, y);
            // The tile interior has contiguous neighbours. Only its two edge
            // samples need the general periodic/domain/tile address path.
            boundary(x, y);
            for (size_t i = 1; i + 1 < center.size(); ++i) {
                double value = diagonal[i] * center[i];
                value -= double(!solid[i] && !solid[i - 1]) * center[i - 1];
                value -= double(!solid[i] && !solid[i + 1]) * center[i + 1];
                value -= double(!solid[i] && !solidAbove[i]) * above[i];
                value -= double(!solid[i] && !solidBelow[i]) * below[i];
                result[i] = value;
            }
            if (center.size() > 1) boundary(x + uint32_t(center.size()) - 1, y);
        }
    }
}
void PixelWindField::Project() {
    PrescribeInflow();
    Pressure.Fill(0);
    size_t fluidCount = 0;
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const auto i = Index(x, y);
            Residual[i] = Solid[i] ? 0 : -DivergenceAt(x, y);
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
        memory::ForEachGridRow([&](auto pressure, auto direction, auto residual, auto product, auto preconditioned, auto diagonal) {
            for (size_t i = 0; i < pressure.size(); ++i) {
                pressure[i] += alpha * direction[i];
                residual[i] -= alpha * product[i];
                preconditioned[i] = diagonal[i] ? residual[i] / diagonal[i] : 0;
            }
        }, Pressure, Direction, Residual, Product, Preconditioned, Diagonal);
        const double nextRz = Dot(Residual, Preconditioned);
        const double beta = rz > 0 ? nextRz / rz : 0;
        memory::ForEachGridRow([&](auto direction, auto preconditioned) {
            for (size_t i = 0; i < direction.size(); ++i) direction[i] = preconditioned[i] + beta * direction[i];
        }, Direction, Preconditioned);
        rz = nextRz;
        ++iterations;
    }
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const auto i = Index(x, y);
            const double left = !Periodic() && x == 0 ? 0 : Pressure[Index(int(x) - 1, y)];
            const double up = !Periodic() && y == 0 ? 0 : Pressure[Index(x, int(y) - 1)];
            U[i] -= PressureWeight(x, y, -1, 0) * (Pressure[i] - left);
            V[i] -= PressureWeight(x, y, 0, -1) * (Pressure[i] - up);
            if (!Periodic() && x + 1 == Width)
                RightU[y] += PressureWeight(x, y, 1, 0) * Pressure[i];
            if (!Periodic() && y + 1 == Height)
                BottomV[x] += PressureWeight(x, y, 0, 1) * Pressure[i];
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
            const auto i = Index(x, y);
            if (Solid[i])
                continue;
            const double divergence = DivergenceAt(x, y);
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
        memory::ForEachGridRow([&](auto u, auto v) {
            for (size_t i = 0; i < u.size(); ++i) speed = std::max({speed, std::abs(u[i]), std::abs(v[i])});
        }, U, V);
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
    auto distance = [this](double a, double b, double extent) { return WindDistance(a - b, extent, Settings.Boundary); };
    for (uint32_t row = 0; row < Height; ++row)
        for (uint32_t col = 0; col < Width; ++col) {
            const auto i = Index(col, row);
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
