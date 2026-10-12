#include "Environment.h"
#include "FieldResponse.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <optional>

namespace pixel {
namespace {
std::optional<size_t> ForceDestination(WorldStateView state, size_t source, const World &world) {
    const auto velocity = world.GetFieldVelocity(uint32_t(source % state.Width), uint32_t(source / state.Width));
    const auto force = FieldResponse::MovementDirection(velocity);
    if (force.X == 0 && force.Y == 0)
        return std::nullopt;
    const int64_t x = int64_t(source % state.Width) + force.X;
    const int64_t y = int64_t(source / state.Width) + force.Y;
    if (x < 0 || y < 0 || x >= state.Width || y >= state.Height)
        return std::nullopt;
    return size_t(y) * state.Width + size_t(x);
}
} // namespace
ForceVector Environment::SampleForce(ForceVector base, uint32_t x, uint32_t y) const {
    return FieldResponse::AddWind(base, Wind.SampleForMaterial(x, y));
}
void Environment::Tick(float duration, World &world) {
    if (!std::isfinite(duration) || duration <= 0)
        return;
    constexpr double stepSeconds = FieldResponse::StepSeconds;
    // Bound catch-up work after a pause; normal frame rates share the same speed.
    TimeRemainder += std::min(double(duration), 0.25);
    while (TimeRemainder >= stepSeconds) {
        Wind.AdvanceBeforeMaterials(world.ReadState(), float(stepSeconds));
        StepForceField(world);
        Wind.UpdateAfterMaterials(world.ReadState(), DirtMovements, float(stepSeconds));
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
        const auto destination = ForceDestination(state, source, world);
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
        world.MoveMaterial(move->Source, move->Destination);
        Processed[move->Destination] = 1;
        if (Wind.NeedsMaterialMotion()) {
            const auto x = uint32_t(move->Destination % state.Width), y = uint32_t(move->Destination / state.Width);
            const float dx = float(int64_t(x) - int64_t(move->Source % state.Width));
            const float dy = float(int64_t(y) - int64_t(move->Source / state.Width));
            // Only successful moves transfer motion. Defer feedback until every
            // chain has finished so earlier blocks cannot alter later blocks' wind.
            DirtMovements.push_back({x, y, {dx * FieldResponse::StepsPerSecond, dy * FieldResponse::StepsPerSecond}});
        }
    }
}
} // namespace pixel
