#include "AdaptiveWindField.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <tuple>

namespace pixel {
performance::MemoryUsage AdaptiveWindField::GetMemoryUsage() const {
    return performance::MeasureObject(*this, Solid, Owner, Cells, Faces, BoundaryFaces);
}

AdaptiveWindField::AdaptiveWindField(WorldMapView world, uint32_t height, const AdaptiveWindSettings &settings)
    : Width(world.Width), Height(height), Settings(settings) {
    if (!settings.BlockSize || settings.BlockSize > 32)
        throw std::invalid_argument("adaptive block size must be 1..32");
    // Same physical parameter bounds and deterministic initial shear as reference.
    PixelWindField initial;
    initial.Reset(Width, Height, settings.Flow);
    if (!world.IsValid() || world.Height < Height || world.CellCount() != size_t(Width) * world.Height)
        throw std::invalid_argument("adaptive wind requires complete matching world");
    Solid.Reset(Width, Height, 2); // Force first geometry rebuild.
    Owner.Reset(Width, Height, -1);
    memory::ChunkedGrid<WindVelocity> previous(Width, Height);
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            previous[size_t(y) * Width + x] = initial.Sample(x + .5f, y + .5f);
            Solid[size_t(y) * Width + x] = world.AtIndex(size_t(y) * Width + x).Type != CellTypes::Empty;
        }
    for (uint32_t y = 0; y < Height; y += settings.BlockSize)
        for (uint32_t x = 0; x < Width; x += settings.BlockSize)
            Split(x, y, std::min(settings.BlockSize, Width - x), std::min(settings.BlockSize, Height - y), previous);
    BuildFaces();
    InitializeFaceVelocities();
    Project();
}
void AdaptiveWindField::Split(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const memory::ChunkedGrid<WindVelocity> &previous) {
    uint32_t solids = 0;
    for (uint32_t py = y; py < y + h; ++py)
        for (uint32_t px = x; px < x + w; ++px)
            solids += Solid[size_t(py) * Width + px];
    if (solids == w * h)
        return;
    if (solids == 0) {
        WindVelocity velocity{};
        const auto id = int32_t(Cells.size());
        for (uint32_t py = y; py < y + h; ++py)
            for (uint32_t px = x; px < x + w; ++px) {
                const auto i = size_t(py) * Width + px;
                Owner[i] = id;
                velocity.X += previous[i].X / float(w * h);
                velocity.Y += previous[i].Y / float(w * h);
            }
        Cells.push_back({x, y, w, h, velocity});
        return;
    }
    // Split the longer axis; mixed leaves terminate at exact material boundaries.
    if (w >= h && w > 1) {
        Split(x, y, w / 2, h, previous);
        Split(x + w / 2, y, w - w / 2, h, previous);
    } else {
        Split(x, y, w, h / 2, previous);
        Split(x, y + h / 2, w, h - h / 2, previous);
    }
}
void AdaptiveWindField::MoveDomain(WorldMapView world, uint32_t height, int64_t offsetX, int64_t offsetY) {
    AdaptiveWindField next(world, height, Settings);
    memory::PageBuffer<WindVelocity> velocities;
    velocities.assign(size_t(world.Width) * height, Settings.Flow.Ambient);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < world.Width; ++x) {
            const double oldX = double(x) + double(offsetX), oldY = double(y) + double(offsetY);
            if (oldX >= 0 && oldY >= 0 && oldX < Width && oldY < Height)
                velocities[size_t(y) * world.Width + x] = Sample(float(oldX) + .5f, float(oldY) + .5f);
        }
    // Unit pixels form exact overlap volumes, including coarse/fine transitions.
    next.SetVelocities(velocities);
    *this = std::move(next);
}
void AdaptiveWindField::SetObstacles(WorldMapView world) {
    if (!world.IsValid() || world.Width != Width || world.Height < Height || world.CellCount() != size_t(world.Width) * world.Height)
        throw std::invalid_argument("adaptive obstacle extent mismatch");
    bool changed = false;
    for (size_t i = 0; i < Solid.size(); ++i)
        changed |= Solid[i] != (world.MaterialAtIndex(i) != CellTypes::Empty);
    if (!changed)
        return;
    memory::ChunkedGrid<WindVelocity> previous(Width, Height);
    for (size_t i = 0; i < Solid.size(); ++i) {
        if (Owner[i] >= 0)
            previous[i] = Cells[size_t(Owner[i])].Velocity;
        Solid[i] = world.MaterialAtIndex(i) != CellTypes::Empty;
    }
    Cells.clear();
    Owner.Fill(-1);
    for (uint32_t y = 0; y < Height; y += Settings.BlockSize)
        for (uint32_t x = 0; x < Width; x += Settings.BlockSize)
            Split(x, y, std::min(Settings.BlockSize, Width - x), std::min(Settings.BlockSize, Height - y), previous);
    BuildFaces();
    InitializeFaceVelocities();
    Project();
}
void AdaptiveWindField::BuildFaces() {
    std::map<std::tuple<int32_t, int32_t, bool>, uint32_t> lengths;
    for (uint32_t y = 0; y < Height; ++y)
        for (uint32_t x = 0; x < Width; ++x) {
            const auto a = Owner[size_t(y) * Width + x];
            if (a < 0)
                continue;
            const auto right = Owner[size_t(y) * Width + (x + 1) % Width];
            const auto down = Owner[size_t((y + 1) % Height) * Width + x];
            if ((Settings.Flow.Boundary == WindBoundaryMode::Periodic || x + 1 < Width)
                && right >= 0 && (right != a || x + 1 == Width))
                ++lengths[{a, right, true}];
            if ((Settings.Flow.Boundary == WindBoundaryMode::Periodic || y + 1 < Height)
                && down >= 0 && (down != a || y + 1 == Height))
                ++lengths[{a, down, false}];
        }
    BoundaryFaces.clear();
    if (Settings.Flow.Boundary == WindBoundaryMode::Open)
        for (uint32_t i = 0; i < Cells.size(); ++i) {
            const auto &c = Cells[i];
            if (c.X == 0)
                BoundaryFaces.push_back({i, true, false, IsWindInflow(WindBoundarySide::Left, Settings.Flow.Ambient), double(c.H), double(c.W), 0});
            if (c.X + c.W == Width)
                BoundaryFaces.push_back({i, true, true, IsWindInflow(WindBoundarySide::Right, Settings.Flow.Ambient), double(c.H), double(c.W), 0});
            if (c.Y == 0)
                BoundaryFaces.push_back({i, false, false, IsWindInflow(WindBoundarySide::Top, Settings.Flow.Ambient), double(c.W), double(c.H), 0});
            if (c.Y + c.H == Height)
                BoundaryFaces.push_back({i, false, true, IsWindInflow(WindBoundarySide::Bottom, Settings.Flow.Ambient), double(c.W), double(c.H), 0});
        }
    Faces.clear();
    for (const auto &[key, length] : lengths) {
        const auto [a, b, horizontal] = key;
        const double distance = .5 * (horizontal ? Cells[a].W + Cells[b].W : Cells[a].H + Cells[b].H);
        Faces.push_back({uint32_t(a), uint32_t(b), horizontal, double(length), distance, 0});
    }
}
void AdaptiveWindField::InitializeFaceVelocities() {
    for (auto &f : BoundaryFaces) {
        const auto &v = f.Inflow ? Settings.Flow.Ambient : Cells[f.Owner].Velocity;
        f.Velocity = f.Horizontal ? v.X : v.Y;
    }
    for (auto &f : Faces) {
        const auto &a = Cells[f.A], &b = Cells[f.B];
        const double wa = f.Horizontal ? a.W : a.H, wb = f.Horizontal ? b.W : b.H;
        f.Velocity = f.Horizontal ? (wb * a.Velocity.X + wa * b.Velocity.X) / (wa + wb)
                                  : (wb * a.Velocity.Y + wa * b.Velocity.Y) / (wa + wb);
    }
}
void AdaptiveWindField::ReconstructVelocities() {
    for (auto &cell : Cells)
        cell.Velocity = {};
    for (const auto &f : BoundaryFaces) {
        auto &c = Cells[f.Owner];
        if (f.Horizontal)
            c.Velocity.X += float(f.Length * f.Velocity / (2 * c.H));
        else
            c.Velocity.Y += float(f.Length * f.Velocity / (2 * c.W));
    }
    for (const auto &f : Faces) {
        auto &a = Cells[f.A], &b = Cells[f.B];
        if (f.Horizontal) {
            a.Velocity.X += float(f.Length * f.Velocity / (2 * a.H));
            b.Velocity.X += float(f.Length * f.Velocity / (2 * b.H));
        } else {
            a.Velocity.Y += float(f.Length * f.Velocity / (2 * a.W));
            b.Velocity.Y += float(f.Length * f.Velocity / (2 * b.W));
        }
    }
}
void AdaptiveWindField::Project() {
    const size_t n = Cells.size();
    std::vector<double> p(n), rhs(n), r(n), d(n), ad(n), diagonal(n), z(n);
    for (const auto &f : Faces) {
        rhs[f.A] -= f.Length * f.Velocity;
        rhs[f.B] += f.Length * f.Velocity;
        if (f.A != f.B) {
            diagonal[f.A] += f.Length / f.Distance;
            diagonal[f.B] += f.Length / f.Distance;
        }
    }
    for (const auto &f : BoundaryFaces) {
        rhs[f.Owner] -= (f.Positive ? 1 : -1) * f.Length * f.Velocity;
        if (!f.Inflow)
            diagonal[f.Owner] += f.Length / f.Distance;
    }
    auto measure = [&](const std::vector<double> &residual) {
        double sum = 0, volume = 0, maximum = 0;
        for (size_t i = 0; i < n; ++i) {
            const double divergence = residual[i] / Cells[i].Volume();
            sum += divergence * divergence * Cells[i].Volume();
            volume += Cells[i].Volume();
            maximum = std::max(maximum, std::abs(divergence));
        }
        Report.RmsDivergence = volume ? std::sqrt(sum / volume) : 0;
        Report.MaxDivergence = maximum;
        Report.Converged = Report.RmsDivergence <= Settings.Flow.DivergenceTolerance;
    };
    auto dot = [](const auto &a, const auto &b) {
        double result = 0;
        for (size_t i = 0; i < a.size(); ++i)
            result += a[i] * b[i];
        return result;
    };
    r = rhs;
    for (size_t i = 0; i < n; ++i)
        d[i] = z[i] = diagonal[i] ? r[i] / diagonal[i] : 0;
    double rz = dot(r, z);
    Report.PressureIterations = 0;
    measure(r);
    while (!Report.Converged && Report.PressureIterations < Settings.Flow.MaxPressureIterations) {
        std::fill(ad.begin(), ad.end(), 0);
        for (const auto &f : Faces) {
            const double flux = (d[f.A] - d[f.B]) * f.Length / f.Distance;
            ad[f.A] += flux;
            ad[f.B] -= flux;
        }
        for (const auto &f : BoundaryFaces)
            if (!f.Inflow)
                ad[f.Owner] += d[f.Owner] * f.Length / f.Distance;
        const double denominator = dot(d, ad);
        if (!(denominator > 0) || !std::isfinite(denominator))
            break;
        const double alpha = rz / denominator;
        for (size_t i = 0; i < n; ++i) {
            p[i] += alpha * d[i];
            r[i] -= alpha * ad[i];
            z[i] = diagonal[i] ? r[i] / diagonal[i] : 0;
        }
        ++Report.PressureIterations;
        measure(r);
        const double next = dot(r, z);
        const double beta = rz > 0 ? next / rz : 0;
        for (size_t i = 0; i < n; ++i)
            d[i] = z[i] + beta * d[i];
        rz = next;
    }
    std::fill(rhs.begin(), rhs.end(), 0);
    for (auto &f : Faces) {
        f.Velocity -= (p[f.B] - p[f.A]) / f.Distance;
        rhs[f.A] += f.Length * f.Velocity;
        rhs[f.B] -= f.Length * f.Velocity;
    }
    for (auto &f : BoundaryFaces) {
        const double normal = f.Positive ? 1 : -1;
        if (!f.Inflow)
            f.Velocity += normal * p[f.Owner] / f.Distance;
        rhs[f.Owner] += normal * f.Length * f.Velocity;
    }
    measure(rhs); // Report the actual shared face flux, including roundoff.
    ReconstructVelocities();
}
bool AdaptiveWindField::IsSolid(uint32_t x, uint32_t y) const { return x < Width && y < Height && Solid[size_t(y) * Width + x]; }
WindVelocity AdaptiveWindField::Sample(float x, float y) const {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height)
        return {};
    const auto owner = Owner[size_t(y) * Width + uint32_t(x)];
    return owner >= 0 ? Cells[size_t(owner)].Velocity : WindVelocity{};
}
void AdaptiveWindField::Transport(double dt) {
    std::vector<WindVelocity> next(Cells.size());
    for (size_t i = 0; i < Cells.size(); ++i)
        next[i] = Cells[i].Velocity;
    for (const auto &f : Faces) {
        const auto &a = Cells[f.A], &b = Cells[f.B];
        const double diffusion = Settings.Flow.Viscosity * dt * f.Length / f.Distance;
        const double dx = (b.Velocity.X - a.Velocity.X) * diffusion, dy = (b.Velocity.Y - a.Velocity.Y) * diffusion;
        next[f.A].X += float(dx / a.Volume());
        next[f.A].Y += float(dy / a.Volume());
        next[f.B].X -= float(dx / b.Volume());
        next[f.B].Y -= float(dy / b.Volume());
    }
    // Diffuse first, then advect momentum with upwind shared face fluxes.
    // Point sampling of constant leaves would pin a sub-cell backtrace to its
    // source leaf forever under the CFL bound.
    const auto diffused = next;
    for (const auto &f : Faces) {
        const auto &upstream = diffused[f.Velocity >= 0 ? f.A : f.B];
        const double flux = dt * f.Length * f.Velocity;
        next[f.A].X -= float(flux * upstream.X / Cells[f.A].Volume());
        next[f.A].Y -= float(flux * upstream.Y / Cells[f.A].Volume());
        next[f.B].X += float(flux * upstream.X / Cells[f.B].Volume());
        next[f.B].Y += float(flux * upstream.Y / Cells[f.B].Volume());
    }
    for (const auto &f : BoundaryFaces) {
        const double outward = (f.Positive ? 1 : -1) * f.Velocity;
        const auto &upstream = outward < 0 ? Settings.Flow.Ambient : diffused[f.Owner];
        const double flux = dt * f.Length * outward / Cells[f.Owner].Volume();
        next[f.Owner].X -= float(flux * upstream.X);
        next[f.Owner].Y -= float(flux * upstream.Y);
    }
    const float blend = float(-std::expm1(-Settings.Flow.Relaxation * dt));
    for (size_t i = 0; i < Cells.size(); ++i)
        Cells[i].Velocity = {std::lerp(next[i].X, Settings.Flow.Ambient.X, blend), std::lerp(next[i].Y, Settings.Flow.Ambient.Y, blend)};
    InitializeFaceVelocities();
    Project();
}
void AdaptiveWindField::Step(float seconds) {
    if (!std::isfinite(seconds) || seconds <= 0)
        return;
    double remaining = std::min(double(seconds), .25);
    Report.Substeps = 0;
    while (remaining > 1e-12) {
        // Bound the actual outgoing face flux per leaf. Reconstructed centre
        // velocities can cancel and must not be used to estimate this CFL bound.
        std::vector<double> outgoing(Cells.size());
        for (const auto &f : Faces) {
            const auto upstream = f.Velocity >= 0 ? f.A : f.B;
            outgoing[upstream] += f.Length * std::abs(f.Velocity) / Cells[upstream].Volume();
        }
        for (const auto &f : BoundaryFaces) {
            const double outward = (f.Positive ? 1 : -1) * f.Velocity;
            outgoing[f.Owner] += f.Length * std::max(0., outward) / Cells[f.Owner].Volume();
        }
        double rate = 1;
        for (const auto value : outgoing)
            rate = std::max(rate, value);
        const double dt = std::min({remaining, .5 / rate, .1 / std::max(.001f, Settings.Flow.Viscosity)});
        Transport(dt);
        remaining -= dt;
        ++Report.Substeps;
    }
}
void AdaptiveWindField::SetVelocities(std::span<const WindVelocity> velocities) {
    if (velocities.size() != Solid.size())
        throw std::invalid_argument("adaptive velocity extent mismatch");
    for (const auto &v : velocities)
        if (!std::isfinite(v.X) || !std::isfinite(v.Y) || std::abs(v.X) > 1000 || std::abs(v.Y) > 1000)
            throw std::invalid_argument("invalid adaptive initial velocity");
    for (auto &c : Cells) {
        c.Velocity = {};
        for (uint32_t y = c.Y; y < c.Y + c.H; ++y)
            for (uint32_t x = c.X; x < c.X + c.W; ++x) {
                c.Velocity.X += float(velocities[size_t(y) * Width + x].X / c.Volume());
                c.Velocity.Y += float(velocities[size_t(y) * Width + x].Y / c.Volume());
            }
    }
    InitializeFaceVelocities();
    Project();
}
void AdaptiveWindField::AddImpulse(float x, float y, float radius, WindVelocity delta) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= Width || y >= Height || !std::isfinite(radius) || radius <= 0
        || !std::isfinite(delta.X) || !std::isfinite(delta.Y) || std::abs(delta.X) > 1000 || std::abs(delta.Y) > 1000)
        throw std::invalid_argument("invalid adaptive wind impulse");
    bool changed = false;
    for (auto &c : Cells) {
        const double dx = WindDistance(c.X + .5 * c.W - x, double(Width), Settings.Flow.Boundary) / radius;
        const double dy = WindDistance(c.Y + .5 * c.H - y, double(Height), Settings.Flow.Boundary) / radius;
        const float weight = float(std::pow(std::max(0.0, 1 - dx * dx - dy * dy), 2));
        changed |= delta.X * weight != 0 || delta.Y * weight != 0;
        if (delta.X * weight != 0)
            c.Velocity.X = std::clamp(c.Velocity.X + delta.X * weight, -1000.f, 1000.f);
        if (delta.Y * weight != 0)
            c.Velocity.Y = std::clamp(c.Velocity.Y + delta.Y * weight, -1000.f, 1000.f);
    }
    if (changed) {
        InitializeFaceVelocities();
        Project();
    }
}
} // namespace pixel
