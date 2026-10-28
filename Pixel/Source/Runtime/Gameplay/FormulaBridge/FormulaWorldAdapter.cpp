#include "FormulaWorldAdapter.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace pixel {
namespace {
constexpr double ContactEpsilon = 1e-10;

// A rectangle dilated by a disk is four face strips and four corner disks. Testing
// both preserves the rounded corners which an expanded AABB would overestimate.
std::optional<double> CircleRectangle(double sx, double sy, double dx, double dy, double x, double y,
                                      double width, double height, double radius) {
    const double nearestX = std::clamp(sx, x, x + width);
    const double nearestY = std::clamp(sy, y, y + height);
    if (std::hypot(sx - nearestX, sy - nearestY) <= radius + ContactEpsilon)
        return 0;

    double first = std::numeric_limits<double>::infinity();
    const auto accept = [&](double t) {
        if (t >= -ContactEpsilon && t <= 1 + ContactEpsilon)
            first = std::min(first, std::clamp(t, 0.0, 1.0));
    };
    if (dx != 0) {
        for (const double face : {x - radius, x + width + radius}) {
            const double t = (face - sx) / dx;
            const double along = sy + t * dy;
            if (along >= y - ContactEpsilon && along <= y + height + ContactEpsilon)
                accept(t);
        }
    }
    if (dy != 0) {
        for (const double face : {y - radius, y + height + radius}) {
            const double t = (face - sy) / dy;
            const double along = sx + t * dx;
            if (along >= x - ContactEpsilon && along <= x + width + ContactEpsilon)
                accept(t);
        }
    }

    const double a = dx * dx + dy * dy;
    if (a != 0) {
        for (const double cx : {x, x + width}) {
            for (const double cy : {y, y + height}) {
                const double ox = sx - cx, oy = sy - cy;
                const double b = ox * dx + oy * dy;
                const double c = ox * ox + oy * oy - radius * radius;
                const double discriminant = b * b - a * c;
                // Relative tolerance only compensates arithmetic at tangency.
                const double tolerance = ContactEpsilon * std::max({1.0, b * b, std::abs(a * c)});
                if (discriminant >= -tolerance)
                    accept((-b - std::sqrt(std::max(0.0, discriminant))) / a);
            }
        }
    }
    return std::isfinite(first) ? std::optional(first) : std::nullopt;
}

bool Earlier(const FormulaContact &candidate, const FormulaContact &current) {
    if (current.Kind == FormulaContactKind::None)
        return true;
    if (candidate.Fraction < current.Fraction - ContactEpsilon)
        return true;
    if (std::abs(candidate.Fraction - current.Fraction) > ContactEpsilon)
        return false;
    if ((candidate.Kind == FormulaContactKind::Solid) != (current.Kind == FormulaContactKind::Solid))
        return candidate.Kind == FormulaContactKind::Solid;
    return CellCoordLess{}(candidate.Cell, current.Cell);
}
} // namespace

WorldPosition FormulaWorldAdapter::Offset(WorldPosition anchor, FormulaVector displacement) {
    anchor.Local[0] += displacement.X;
    anchor.Local[1] += displacement.Y;
    anchor.Normalize();
    return anchor;
}

FormulaContact FormulaWorldAdapter::Sweep(const World &world, WorldPosition from, WorldPosition to, double radius,
                                          std::span<const ActorSnapshot> actors) {
    from.Normalize();
    to.Normalize();
    if (!std::isfinite(radius) || radius < 0 || radius > 16)
        throw std::invalid_argument("invalid formula collision radius");
    const auto origin = from.Cell();
    const auto start = from.RelativeTo(origin), end = to.RelativeTo(origin);
    const double dx = end[0] - start[0], dy = end[1] - start[1];
    // The runtime guarantees <=10 cells per fixed step. Keep this public entry
    // bounded too, so a malformed caller cannot scan an arbitrary world region.
    if (!std::isfinite(dx) || !std::isfinite(dy) || std::hypot(dx, dy) > 64)
        throw std::invalid_argument("formula sweep exceeds local work bound");
    // Cell faces are closed: a lower bound exactly on an integer also touches
    // the preceding cell. Include comparison tolerance in this broad phase so
    // rounding cannot drop a contact which the exact circle test would accept.
    const int64_t left = int64_t(std::ceil(std::min(start[0], end[0]) - radius - ContactEpsilon)) - 1;
    const int64_t right = int64_t(std::floor(std::max(start[0], end[0]) + radius + ContactEpsilon));
    const int64_t top = int64_t(std::ceil(std::min(start[1], end[1]) - radius - ContactEpsilon)) - 1;
    const int64_t bottom = int64_t(std::floor(std::max(start[1], end[1]) + radius + ContactEpsilon));
    const auto bounds = world.GetSimulationBounds();
    FormulaContact first;
    for (int64_t y = top; y <= bottom; ++y) {
        for (int64_t x = left; x <= right; ++x) {
            const CellCoord cell{origin.X + x, origin.Y + y};
            FormulaContactKind kind = FormulaContactKind::OutOfSimulation;
            if (bounds.Contains(cell)) {
                const auto query = world.TryGetCell(cell.X, cell.Y);
                if (!query)
                    kind = FormulaContactKind::NotReady;
                else if (BlocksCharacter(query->Type))
                    kind = FormulaContactKind::Solid;
                else
                    continue;
            }
            const auto contact = CircleRectangle(start[0], start[1], dx, dy, double(x), double(y), 1, 1, radius);
            if (!contact)
                continue;
            FormulaContact candidate{kind, *contact, {}, cell};
            if (Earlier(candidate, first))
                first = candidate;
        }
    }
    for (const auto &actor : actors) {
        if (actor.Kind != ActorKind::TrainingDummy) continue;
        const auto position = actor.Position.RelativeTo(origin);
        const auto last = Offset(actor.Position, {Body::Width - ContactEpsilon, Body::Height - ContactEpsilon}).Cell();
        if (!bounds.Contains(actor.Position.Cell()) || !bounds.Contains(last)) continue;
        // Broad phase also keeps distant actors out of the quadratic calculation.
        if (position[0] > std::max(start[0], end[0]) + radius + ContactEpsilon
            || position[0] + Body::Width < std::min(start[0], end[0]) - radius - ContactEpsilon
            || position[1] > std::max(start[1], end[1]) + radius + ContactEpsilon
            || position[1] + Body::Height < std::min(start[1], end[1]) - radius - ContactEpsilon) continue;
        const auto contact = CircleRectangle(start[0], start[1], dx, dy, position[0], position[1],
                                              Body::Width, Body::Height, radius);
        // Terrain and unavailable regions win ties. Targets never bypass a wall
        // or mutate health/lifetime; repeated hits keep the same actor identity.
        if (contact && (first.Kind == FormulaContactKind::None || *contact < first.Fraction - ContactEpsilon))
            first = {FormulaContactKind::Actor, *contact, {}, actor.Position.Cell(), actor.Id};
    }
    first.Position = Offset(from, {dx * first.Fraction, dy * first.Fraction});
    return first;
}

FormulaDigResult FormulaWorldAdapter::Dig(World &world, WorldPosition position, double radius,
                                         const std::optional<MiningCollector> &collector) {
    position.Normalize();
    if (!std::isfinite(radius) || radius < 0 || radius > 16)
        throw std::invalid_argument("invalid formula dig radius");
    const CellCoord origin = position.Cell();
    const auto local = position.RelativeTo(origin);
    // Match the closed-circle tolerance when selecting candidate cell centers;
    // otherwise decimal radius subtraction can round a boundary center out.
    const int64_t left = radius == 0 ? 0 : int64_t(std::ceil(local[0] - radius - .5 - ContactEpsilon));
    const int64_t right = radius == 0 ? 0 : int64_t(std::floor(local[0] + radius - .5 + ContactEpsilon));
    const int64_t top = radius == 0 ? 0 : int64_t(std::ceil(local[1] - radius - .5 - ContactEpsilon));
    const int64_t bottom = radius == 0 ? 0 : int64_t(std::floor(local[1] + radius - .5 + ContactEpsilon));
    FormulaDigResult result;
    for (int64_t y = top; y <= bottom; ++y) {
        for (int64_t x = left; x <= right; ++x) {
            if (radius != 0 && std::hypot(double(x) + .5 - local[0], double(y) + .5 - local[1]) > radius + ContactEpsilon)
                continue;
            const CellCoord cell{origin.X + x, origin.Y + y};
            if (!world.GetSimulationBounds().Contains(cell) || !world.TryGetCell(cell.X, cell.Y)) {
                ++result.NotReady;
                continue;
            }
            switch (MineCell(world, cell, collector).Status) {
            case MiningStatus::Applied: ++result.Applied; break;
            case MiningStatus::Blocked: ++result.Blocked; break;
            case MiningStatus::NotReady: ++result.NotReady; break;
            case MiningStatus::InventoryFull: ++result.InventoryFull; break;
            }
        }
    }
    return result;
}

FormulaWindResult FormulaWorldAdapter::Wind(World &world, WorldPosition position, double radius, FormulaVector velocity) {
    if (!std::isfinite(radius) || radius < 0 || radius > 16 || !std::isfinite(velocity.X) || !std::isfinite(velocity.Y)
        || std::hypot(velocity.X, velocity.Y) > 600 + ContactEpsilon)
        throw std::invalid_argument("invalid formula wind impulse");
    if (radius == 0)
        return FormulaWindResult::ZeroRadius;
    if (radius < 1.0 / 256)
        return FormulaWindResult::BelowResolution;
    if (velocity.X == 0 && velocity.Y == 0)
        return FormulaWindResult::ZeroImpulse;

    const auto available = WindAvailability(world, position);
    if (available != FormulaWindResult::Applied) return available;
    return world.AddWindImpulse(position, float(radius), {float(velocity.X), float(velocity.Y)})
        ? FormulaWindResult::Applied : FormulaWindResult::Unavailable;
}

FormulaWindResult FormulaWorldAdapter::WindAvailability(const World &world, WorldPosition position) {
    const auto &simulation = world.GetEnvironment().GetWindSimulation();
    const auto &domain = simulation.GetDomain();
    if (!world.IsWindEnabled() || domain.Width == 0 || domain.Height == 0 || simulation.GetKind() == WindStrategyKind::Empty)
        return FormulaWindResult::Unavailable;
    if (simulation.GetKind() != WindStrategyKind::Coarse)
        return FormulaWindResult::BackendUnsupported;
    const auto &coarse = simulation.GetCoarse();
    const auto &settings = coarse.GetSettings();
    if (settings.Columns == 0 || settings.Rows == 0 || settings.Columns > 1024 / settings.Rows)
        return FormulaWindResult::GridUnsupported;
    if (!coarse.IsEnabled())
        return FormulaWindResult::Unavailable;

    position.Normalize();
    const auto local = position.RelativeTo(domain.Origin);
    if (local[0] < 0 || local[1] < 0 || local[0] >= domain.Width || local[1] >= domain.Height)
        return FormulaWindResult::Unavailable;
    return FormulaWindResult::Applied;
}

const char *FormulaWorldAdapter::WindResultName(FormulaWindResult result) {
    switch (result) {
    case FormulaWindResult::Applied: return "WindApplied";
    case FormulaWindResult::ZeroRadius: return "WindZeroRadius";
    case FormulaWindResult::BelowResolution: return "WindBelowResolution";
    case FormulaWindResult::ZeroImpulse: return "WindZeroImpulse";
    case FormulaWindResult::Unavailable: return "WindUnavailable";
    case FormulaWindResult::BackendUnsupported: return "WindBackendUnsupported";
    case FormulaWindResult::GridUnsupported: return "WindGridUnsupported";
    }
    throw std::logic_error("unknown formula wind result");
}
} // namespace pixel
