#include "Environment.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace pixel {
namespace {
std::optional<size_t> ForceDestination(WorldStateView state, size_t source, const Environment &environment) {
    auto force = state.Cells[source].Force;
    if (environment.GetWind().IsEnabled()) {
        const auto velocity = environment.SampleFieldVelocity(force, float(source % state.Width) + 0.5f, float(source / state.Width) + 0.5f);
        // Preserve the existing one-neighbour-per-step movement model. A base
        // direction represents 60 cells/s; a 30 cells/s dead zone avoids jitter.
        auto direction = [](float speed) {
            if (speed > 30)
                return 1;
            if (speed < -30)
                return -1;
            return 0;
        };
        force = {direction(velocity.X), direction(velocity.Y)};
    }
    if (force.X == 0 && force.Y == 0)
        return std::nullopt;
    const int64_t x = int64_t(source % state.Width) + force.X;
    const int64_t y = int64_t(source / state.Width) + force.Y;
    if (x < 0 || y < 0 || x >= state.Width || y >= state.Height)
        return std::nullopt;
    return size_t(y) * state.Width + size_t(x);
}
} // namespace
WindVelocity Environment::SampleFieldVelocity(ForceDirection base, float x, float y) const {
    const auto wind = Wind.Sample(x, y);
    return {float(base.X) * 60 + wind.X, float(base.Y) * 60 + wind.Y};
}
void Environment::Tick(float duration, World &world) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    constexpr double stepSeconds = 1.0 / 60.0;
    // Bound catch-up work after a pause; normal frame rates share the same speed.
    TimeRemainder += std::min(double(duration), 0.25);
    while (TimeRemainder >= stepSeconds) {
        Wind.Step(float(stepSeconds));
        StepForceField(world);
        Wind.CoupleDirt(world.ReadState(), DirtMovements, float(stepSeconds));
        TimeRemainder -= stepSeconds;
    }
}
void Environment::StepForceField(World &world) {
    const auto state = world.ReadState();
    DirtMovements.clear();
    Processed.assign(state.Cells.size(), 0);
    // Stable row-major priority resolves competing destinations deterministically.
    for (size_t source = 0; source < state.Cells.size(); ++source)
        if (state.Cells[source].Type == CellTypes::Dirt && !Processed[source])
            ResolveMovementChain(world, state, source);
}
void Environment::ResolveMovementChain(World &world, WorldStateView state, size_t source) {
    MovementChain.clear();
    bool canMove = false;
    // Follow occupied destinations to find space. Iteration also handles long
    // stacks without recursion; revisiting a cell blocks closed cycles.
    while (state.Cells[source].Type == CellTypes::Dirt && !Processed[source]) {
        Processed[source] = 1;
        const auto destination = ForceDestination(state, source, *this);
        if (!destination)
            break;
        MovementChain.push_back({source, *destination});
        if (state.Cells[*destination].Type == CellTypes::Empty) {
            canMove = true;
            break;
        }
        source = *destination;
    }
    if (!canMove)
        return;
    // Vacate the front first, then let following blocks advance one cell each.
    // Only Type moves. Each source and destination retains its own force field.
    for (auto move = MovementChain.rbegin(); move != MovementChain.rend(); ++move) {
        auto from = state.Cells[move->Source];
        auto to = state.Cells[move->Destination];
        to.Type = from.Type;
        from.Type = CellTypes::Empty;
        world.SetCell(uint32_t(move->Source % state.Width), uint32_t(move->Source / state.Width), from);
        world.SetCell(uint32_t(move->Destination % state.Width), uint32_t(move->Destination / state.Width), to);
        Processed[move->Destination] = 1;
        if (Wind.IsEnabled()) {
            const auto x = uint32_t(move->Destination % state.Width), y = uint32_t(move->Destination / state.Width);
            const float dx = float(int64_t(x) - int64_t(move->Source % state.Width));
            const float dy = float(int64_t(y) - int64_t(move->Source / state.Width));
            // Only successful moves transfer motion. Defer feedback until every
            // chain has finished so earlier blocks cannot alter later blocks' wind.
            DirtMovements.push_back({x, y, {dx * 60, dy * 60}});
        }
    }
}
} // namespace pixel
