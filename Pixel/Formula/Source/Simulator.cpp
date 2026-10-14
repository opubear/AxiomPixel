#include "Formula/Simulator.h"
#include "Node.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <numbers>
#include <queue>
#include <sstream>
#include <tuple>
#include <utility>

namespace pixel::formula {
namespace {
template <class... T>
struct Overload : T... {
    using T::operator()...;
};
double Checked(double value) {
    if (!std::isfinite(value))
        throw std::overflow_error("formula arithmetic produced a non-finite value");
    return value;
}
void CheckPosition(Vec2 p) {
    if (!std::isfinite(p.X) || !std::isfinite(p.Y))
        throw std::invalid_argument("position must be finite");
}
Tick AddTicks(Tick a, Tick b) {
    if (b > std::numeric_limits<Tick>::max() - a)
        throw std::overflow_error("formula clock overflow");
    return a + b;
}
Vec2 Velocity(double angle, double magnitude) {
    const auto radians = std::remainder(angle, 360.0) * std::numbers::pi / 180.0;
    return {Checked(std::cos(radians) * magnitude), Checked(std::sin(radians) * magnitude)};
}
struct Modifier {
    ModifierKind Kind;
    double Value;
};
struct EvaluationContext {
    CastId Cast;
    Vec2 Origin;
    double Angle;
    std::vector<Modifier> Modifiers;
    std::vector<Formula> Payloads;
};
struct Activation {
    Formula Expression;
    EvaluationContext Context;
};
struct Expiration {
    ProjectileId Id;
};
struct Job {
    Tick At;
    std::uint64_t Order;
    std::variant<Activation, Expiration> Data;
};
struct LaterJob {
    bool operator()(const Job &a, const Job &b) const {
        // A lifetime covers [spawn, expiration); expiry wins at the boundary.
        return std::tuple(a.At, !std::holds_alternative<Expiration>(a.Data), a.Order)
             > std::tuple(b.At, !std::holds_alternative<Expiration>(b.Data), b.Order);
    }
};
struct Projectile {
    ProjectileId Id;
    Tick Born, ExpiresAt;
    EvaluationContext Context;
    Vec2 Velocity;
    double Damage;
    Vec2 Position(Tick now) const {
        const double seconds = static_cast<double>(now - Born) / 60.0;
        return {Checked(Context.Origin.X + Velocity.X * seconds), Checked(Context.Origin.Y + Velocity.Y * seconds)};
    }
};
} // namespace

struct SimulatorState {
    Tick Now = 0;
    std::uint64_t NextOrder = 0;
    CastId NextCast = 1;
    ProjectileId NextProjectile = 1;
    std::priority_queue<Job, std::vector<Job>, LaterJob> Jobs;
    std::map<ProjectileId, Projectile> Active;
    std::vector<TraceEntry> Log;

    void Emit(const EvaluationContext &context, Event event) {
        Log.push_back({Now, context.Cast, context.Origin, std::move(event)});
    }
    void Schedule(Tick at, const Formula &formula, const EvaluationContext &context) {
        if (at == Now)
            Activate(formula, context);
        else
            Jobs.push({at, NextOrder++, Activation{formula, context}});
    }
    void EmitBolt(const BoltNode &node, EvaluationContext context) {
        double damage = node.Damage, speed = node.Speed;
        // Wrappers are encountered outside-in; apply inner transforms first.
        for (auto it = context.Modifiers.rbegin(); it != context.Modifiers.rend(); ++it) {
            switch (it->Kind) {
            case ModifierKind::AddDamage:
                damage = std::max(0.0, Checked(damage + it->Value));
                break;
            case ModifierKind::MultiplyDamage:
                damage = Checked(damage * it->Value);
                break;
            case ModifierKind::MultiplySpeed:
                speed = Checked(speed * it->Value);
                break;
            }
        }
        const auto expiresAt = AddTicks(Now, node.Lifetime);
        const auto velocity = Velocity(context.Angle, speed);
        const auto id = NextProjectile++;
        Emit(context, BoltSpawn{id, velocity, damage, node.Lifetime});
        context.Modifiers.clear();
        // Multiple OnHit decorators register in inner-to-outer order.
        std::reverse(context.Payloads.begin(), context.Payloads.end());
        Active.emplace(id, Projectile{id, Now, expiresAt, std::move(context), velocity, damage});
        Jobs.push({expiresAt, NextOrder++, Expiration{id}});
    }
    void DiagnoseImmediate(const EvaluationContext &context) {
        for (const auto &modifier : context.Modifiers)
            Emit(context, Diagnostic{std::string(ModifierName(modifier.Kind)) + " ignored: effect has no projectile damage/speed"});
        if (!context.Payloads.empty())
            Emit(context, Diagnostic{"on_hit ignored: effect has no projectile carrier"});
    }
    void Activate(const Formula &formula, EvaluationContext context) {
        std::visit(Overload{
                       [](const EmptyNode &) {},
                       [&](const BoltNode &n) { EmitBolt(n, context); },
                       [&](const WindNode &n) {
                           DiagnoseImmediate(context);
                           Emit(context, WindImpulse{n.Radius, Velocity(context.Angle, n.Impulse)});
                       },
                       [&](const DigNode &n) { DiagnoseImmediate(context); Emit(context, DigArea{n.Radius}); },
                       [&](const SequenceNode &n) {
                           for (const auto &child : n.Children)
                               Activate(child, context);
                       },
                       [&](const ModifyNode &n) {
                           context.Modifiers.push_back({n.Kind, n.Value});
                           Activate(n.Child, context);
                       },
                       [&](const DelayNode &n) { Schedule(AddTicks(Now, n.Ticks), n.Child, context); },
                       [&](const RepeatNode &n) {
                           Tick at = Now;
                           for (std::uint32_t i = 0; i < n.Count; ++i) {
                               Schedule(at, n.Child, context);
                               if (i + 1 < n.Count)
                                   at = AddTicks(at, n.Interval);
                           }
                       },
                       [&](const FanNode &n) {
                           for (std::uint32_t i = 0; i < n.Count; ++i) {
                               auto branch = context;
                               branch.Angle = Checked(context.Angle + Checked((static_cast<double>(i) - (static_cast<double>(n.Count) - 1) / 2) * n.SpacingDegrees));
                               Activate(n.Child, std::move(branch));
                           }
                       },
                       [&](const OnHitNode &n) {
                           context.Payloads.push_back(n.Payload);
                           Activate(n.Carrier, context);
                       }},
                   FormulaAccess::Get(formula).Data);
    }
    void Expire(ProjectileId id) {
        const auto found = Active.find(id);
        if (found == Active.end())
            return; // An earlier hit already consumed it.
        // Retirement must survive a failure to represent the final position.
        const auto retired = Active.extract(found);
        const auto &projectile = retired.mapped();
        Log.push_back({Now, projectile.Context.Cast, projectile.Position(Now), ProjectileExpired{id}});
    }
};

Simulator::Simulator() : State(std::make_unique<SimulatorState>()) {}
Simulator::~Simulator() = default;
CastId Simulator::Cast(const Formula &formula, Vec2 origin, double angleDegrees) {
    FormulaAccess::Get(formula);
    CheckPosition(origin);
    if (!std::isfinite(angleDegrees))
        throw std::invalid_argument("angle must be finite");
    const auto id = State->NextCast++;
    State->Jobs.push({State->Now, State->NextOrder++, Activation{formula, {id, origin, angleDegrees, {}, {}}}});
    return id;
}
void Simulator::AdvanceTo(Tick tick) {
    if (tick < State->Now)
        throw std::invalid_argument("cannot move the formula clock backwards");
    while (!State->Jobs.empty() && State->Jobs.top().At <= tick) {
        auto job = State->Jobs.top();
        State->Jobs.pop();
        State->Now = job.At;
        if (auto activation = std::get_if<Activation>(&job.Data))
            State->Activate(activation->Expression, std::move(activation->Context));
        else
            State->Expire(std::get<Expiration>(job.Data).Id);
    }
    State->Now = tick;
}
bool Simulator::Hit(ProjectileId id, Vec2 position) {
    const auto found = State->Active.find(id);
    if (found == State->Active.end())
        return false;
    // Another job at this tick may have thrown before its expiry was processed.
    if (State->Now >= found->second.ExpiresAt) {
        State->Active.erase(found);
        return false;
    }
    CheckPosition(position);
    const auto &carrier = found->second;
    if (!carrier.Context.Payloads.empty()) {
        const auto at = AddTicks(State->Now, 1);
        // Event payloads deliberately inherit no modifiers or trigger bindings.
        const EvaluationContext payloadContext{carrier.Context.Cast, position, carrier.Context.Angle, {}, {}};
        for (const auto &payload : carrier.Context.Payloads)
            State->Jobs.push({at, State->NextOrder++, Activation{payload, payloadContext}});
    }
    State->Log.push_back({State->Now, carrier.Context.Cast, position, ProjectileHit{id}});
    State->Active.erase(found);
    return true;
}
Tick Simulator::Now() const { return State->Now; }
std::vector<ProjectileView> Simulator::Projectiles() const {
    std::vector<ProjectileView> result;
    for (const auto &[id, p] : State->Active) {
        if (State->Now < p.ExpiresAt)
            result.push_back({id, p.Context.Cast, p.Position(State->Now), p.Velocity, p.Damage, p.ExpiresAt});
    }
    return result;
}
const std::vector<TraceEntry> &Simulator::Trace() const { return State->Log; }
std::string FormatTrace(const TraceEntry &entry) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3) << "tick=" << entry.At << " cast=" << entry.Cast
        << " pos=(" << entry.Position.X << ',' << entry.Position.Y << ") ";
    std::visit(Overload{
                   [&](const BoltSpawn &e) { out << "bolt id=" << e.Id << " damage=" << e.Damage << " velocity=(" << e.Velocity.X << ',' << e.Velocity.Y << ") lifetime=" << e.Lifetime; },
                   [&](const WindImpulse &e) { out << "wind radius=" << e.Radius << " impulse=(" << e.Velocity.X << ',' << e.Velocity.Y << ')'; },
                   [&](const DigArea &e) { out << "dig radius=" << e.Radius; },
                   [&](const ProjectileHit &e) { out << "hit id=" << e.Id; },
                   [&](const ProjectileExpired &e) { out << "expired id=" << e.Id; },
                   [&](const Diagnostic &e) { out << "diagnostic: " << e.Message; }},
               entry.Data);
    return out.str();
}
} // namespace pixel::formula
