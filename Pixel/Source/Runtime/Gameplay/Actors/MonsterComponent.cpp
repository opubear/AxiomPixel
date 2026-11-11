#include "MonsterComponent.h"
#include "Actor.h"
#include "BodyComponent.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixel {
namespace {
std::array<double, 2> Centre(const Body &body, CellCoord reference) {
    auto point = body.GetWorldPosition().RelativeCellsTo(reference);
    point[0] += CentimetersToCells(body.Size[0]) * .5;
    point[1] += CentimetersToCells(body.Size[1]) * .5;
    return point;
}

bool ClearLine(WorldMapView terrain, CellCoord reference, std::array<double, 2> from, std::array<double, 2> to) {
    const double dx = to[0] - from[0], dy = to[1] - from[1];
    // Catalog detection is at most 64 cells. Bound traversal before converting
    // a potentially distant target to grid coordinates.
    if (std::max(std::abs(dx), std::abs(dy)) > 128) return false;
    CellCoord cell{int64_t(std::floor(from[0])), int64_t(std::floor(from[1]))};
    const CellCoord last{int64_t(std::floor(to[0])), int64_t(std::floor(to[1]))};
    const auto clear = [&](CellCoord local) {
        const auto x = reference.X + local.X - terrain.Origin.X;
        const auto y = reference.Y + local.Y - terrain.Origin.Y;
        if (x < 0 || y < 0 || x >= terrain.Width || y >= terrain.Height) return false;
        const auto material = terrain.TryAtLocal(uint32_t(x), uint32_t(y));
        return material && !BlocksCharacter(material->Type);
    };

    const int stepX = (dx > 0) - (dx < 0), stepY = (dy > 0) - (dy < 0);
    const double lengthX = std::abs(dx), lengthY = std::abs(dy);
    const int maximumCells = 1 + int(std::abs(last.X - cell.X) + std::abs(last.Y - cell.Y));
    for (int visited = 0; visited < maximumCells; ++visited) {
        if (!clear(cell)) return false;
        if (cell == last) return true;
        // Endpoints belong to their floor cell. In particular, a negative
        // direction must not step beyond an endpoint lying on a grid edge.
        if (cell.X == last.X) { cell.Y += stepY; continue; }
        if (cell.Y == last.Y) { cell.X += stepX; continue; }

        // Compare boundary distances by cross multiplication. Recompute from
        // the start each time: accumulated reciprocal steps drift at vertices
        // (e.g. a 1:7 slope) and would skip one of the touching side cells.
        const double distanceX = stepX > 0 ? cell.X + 1 - from[0] : from[0] - cell.X;
        const double distanceY = stepY > 0 ? cell.Y + 1 - from[1] : from[1] - cell.Y;
        const double nextX = distanceX * lengthY, nextY = distanceY * lengthX;
        if (nextX < nextY) {
            cell.X += stepX;
        } else if (nextY < nextX) {
            cell.Y += stepY;
        } else {
            // At an interior grid corner, either touching side cell blocks
            // sight; endpoints retain the floor-cell convention above.
            if (distanceX > 0 && distanceX < lengthX
                && (!clear({cell.X + stepX, cell.Y}) || !clear({cell.X, cell.Y + stepY}))) return false;
            cell.X += stepX;
            cell.Y += stepY;
        }
    }
    return false;
}
}

const char *MonsterActionName(MonsterAction action) {
    switch (action) {
    case MonsterAction::Dormant: return "Dormant";
    case MonsterAction::Idle: return "Idle";
    case MonsterAction::Move: return "Move";
    case MonsterAction::Wake: return "Wake";
    case MonsterAction::Attack: return "Attack";
    case MonsterAction::Hurt: return "Hurt";
    case MonsterAction::Death: return "Death";
    }
    throw std::invalid_argument("unknown monster action");
}

MonsterComponent::MonsterComponent(Actor &owner, const MonsterDefinition &definition, MonsterState state)
    : Component(owner), Definition(definition), Motion(*owner.GetComponent<BodyComponent>()), State(std::move(state)) {
    MonsterActionName(State.Action);
    State.Home.Normalize();
    if (State.Definition != Definition.Id || State.ActionTicks > 3600 || State.CooldownTicks > Definition.AttackCooldownTicks
        || (!Definition.WakeTicks && (State.Action == MonsterAction::Wake || State.Action == MonsterAction::Dormant)))
        throw std::invalid_argument("invalid monster state");
    if ((State.Action == MonsterAction::Attack && State.ActionTicks >= Definition.AttackDurationTicks)
        || (State.Action == MonsterAction::Hurt && State.ActionTicks >= Definition.HurtTicks)
        || (State.Action == MonsterAction::Wake && State.ActionTicks >= Definition.WakeTicks)
        || (State.Action == MonsterAction::Death && State.ActionTicks >= Definition.DeathTicks))
        throw std::invalid_argument("monster state exceeded its transition tick");
}

void MonsterComponent::Enter(MonsterAction action) {
    if (State.Action == action) return;
    State.Action = action;
    State.ActionTicks = 0;
}

void MonsterComponent::Hit(bool died) {
    Enter(died ? MonsterAction::Death : MonsterAction::Hurt);
    State.ActionTicks = 0;
}

bool MonsterComponent::CanStrike(WorldMapView terrain, const Body &body, const Body &target) const {
    const auto reference = body.GetWorldPosition().Cell();
    const auto from = Centre(body, reference), to = Centre(target, reference);
    const double dx = to[0] - from[0], dy = to[1] - from[1];
    const double gapX = std::max(0., std::abs(dx) - CentimetersToCells(body.Size[0] + target.Size[0]) * .5);
    const double gapY = std::max(0., std::abs(dy) - CentimetersToCells(body.Size[1] + target.Size[1]) * .5);
    return (std::abs(dx) < .1 || (dx < 0) == State.FacingLeft)
        && std::hypot(gapX, gapY) <= Definition.AttackRangeCells
        && ClearLine(terrain, reference, from, to);
}

MonsterStep MonsterComponent::Step(WorldMapView terrain, const std::optional<Body> &target) {
    const auto &body = Motion.GetBody();
    if (!body.HasTerrain(terrain)) return {};
    if (State.CooldownTicks) --State.CooldownTicks;
    State.ActionTicks = State.ActionTicks == 3600 ? 0 : State.ActionTicks + 1;
    if (State.Action == MonsterAction::Death)
        return {true, false, State.ActionTicks >= Definition.DeathTicks};
    if (State.Action == MonsterAction::Hurt || State.Action == MonsterAction::Wake) {
        const auto duration = State.Action == MonsterAction::Hurt ? Definition.HurtTicks : Definition.WakeTicks;
        if (!Definition.Flying) Motion.MoveMonster(terrain, 0, false, 0);
        if (State.ActionTicks >= duration) Enter(MonsterAction::Idle);
        return {true};
    }
    if (State.Action == MonsterAction::Attack) {
        const bool strike = State.ActionTicks == Definition.AttackWindupTicks && target && CanStrike(terrain, body, *target);
        if (!Definition.Flying) Motion.MoveMonster(terrain, 0, false, 0);
        if (State.ActionTicks >= Definition.AttackDurationTicks) Enter(MonsterAction::Idle);
        return {true, strike};
    }

    const auto reference = body.GetWorldPosition().Cell();
    const auto from = Centre(body, reference);
    const auto to = target ? Centre(*target, reference) : from;
    const double dx = to[0] - from[0], dy = to[1] - from[1];
    const bool chasing = target && std::hypot(dx, dy) <= Definition.DetectionCells
        && ClearLine(terrain, reference, from, to);
    if (State.Action == MonsterAction::Dormant) {
        if (chasing) { State.FacingLeft = dx < 0; Enter(MonsterAction::Wake); }
        if (!Definition.Flying) Motion.MoveMonster(terrain, 0, false, 0);
        return {true};
    }
    if (chasing) {
        if (std::abs(dx) > .1) State.FacingLeft = dx < 0;
        if (!State.CooldownTicks && CanStrike(terrain, body, *target)) {
            Enter(MonsterAction::Attack);
            State.CooldownTicks = Definition.AttackCooldownTicks;
            return {true};
        }
    } else {
        const auto home = State.Home.RelativeCellsTo(reference);
        const auto position = body.GetWorldPosition().RelativeCellsTo(reference);
        if (position[0] < home[0] - Definition.PatrolCells) State.FacingLeft = false;
        if (position[0] > home[0] + Definition.PatrolCells) State.FacingLeft = true;
        // Pause between patrol walks so Idle remains a real gameplay state.
        if (State.Action == MonsterAction::Idle && State.ActionTicks < 60) {
            if (!Definition.Flying) Motion.MoveMonster(terrain, 0, false, 0);
            return {true};
        }
        if (State.Action == MonsterAction::Move && State.ActionTicks >= 180) {
            Enter(MonsterAction::Idle);
            return {true};
        }
    }

    const auto previous = body.GetWorldPosition();
    const int direction = State.FacingLeft ? -1 : 1;
    const float speed = CellsToCentimeters(Definition.SpeedCellsPerSecond);
    if (Definition.Flying) {
        const double homeY = State.Home.RelativeCellsTo(reference)[1] + Definition.BodySizeCells[1] * .5;
        const double vertical = chasing ? dy : homeY - from[1];
        const double horizontal = chasing ? dx : direction * 4.;
        const double length = std::max(1., std::hypot(horizontal, vertical));
        Motion.FlyMonster(terrain, float(horizontal / length) * speed, float(vertical / length) * speed);
    } else {
        // Jump only when the next horizontal step is obstructed. Body retains
        // terrain collision, water response and the existing fixed-step rules.
        auto probe = body;
        auto probePosition = previous;
        probePosition.Local[0] += direction * speed / 60;
        probe.RestoreState({probePosition});
        Motion.MoveMonster(terrain, direction, chasing && body.Grounded && !probe.CanOccupy(terrain), speed);
    }
    const auto before = previous.RelativeCellsTo(reference), after = body.GetWorldPosition().RelativeCellsTo(reference);
    const bool moved = std::hypot(after[0] - before[0], after[1] - before[1]) > .0001;
    if (!moved && !chasing) State.FacingLeft = !State.FacingLeft;
    Enter(moved ? MonsterAction::Move : MonsterAction::Idle);
    return {true};
}
} // namespace pixel
