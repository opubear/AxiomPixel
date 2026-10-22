#include "FormulaRuntime.h"
#include "FormulaValidation/FormulaAnalysis.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <tuple>

namespace pixel {
namespace {
constexpr size_t MaxRoots = 16, MaxEffects = 256, MaxWind = 64, MaxActivations = 8, MaxDiagnostics = 256;
struct Root {
    uint64_t Id, Version;
    WorldPosition Anchor;
    formula::validation::Analysis Budget;
    formula::Simulator Simulator;
    std::vector<formula::TraceEntry> Batch;
};
struct Collision {
    Root *Owner;
    formula::ProjectileId Projectile;
    FormulaContact Contact;
};
} // namespace

struct FormulaRuntime::State {
    formula::Formula Active = formula::Formula::Bolt();
    formula::validation::Analysis ActiveBudget;
    uint64_t Version = 1, Epoch = 1, NextCast = 1;
    uint64_t RetiredWind = 0, Accepted = 0, Rejected = 0, WindCalls = 0;
    std::string Error;
    std::vector<std::unique_ptr<Root>> Roots;
    std::vector<FormulaProjectile> Projectiles;
    std::deque<FormulaRuntimeDiagnostic> Diagnostics;

    State() {
        const auto analysis = formula::validation::Analyze(Active);
        if (!analysis)
            throw std::logic_error("default bolt failed gameplay analysis");
        ActiveBudget = analysis.Budget;
        Roots.reserve(MaxRoots);
        Projectiles.reserve(MaxEffects);
    }

    void Record(uint64_t cast, formula::Tick tick, std::string message) {
        if (Diagnostics.size() == MaxDiagnostics)
            Diagnostics.pop_front();
        Diagnostics.push_back({Epoch, cast, tick, std::move(message)});
    }

    bool Reject(std::string reason) {
        Error = std::move(reason);
        ++Rejected;
        Record(0, 0, Error);
        return false;
    }

    FormulaRuntimeStats Statistics() const {
        FormulaRuntimeStats result;
        result.Roots = Roots.size();
        result.Projectiles = Projectiles.size();
        result.SnapshotBytes = Projectiles.capacity() * sizeof(FormulaProjectile);
        for (const auto &entry : Diagnostics)
            result.DiagnosticBytes += sizeof(FormulaRuntimeDiagnostic) + entry.Message.capacity() + 1;
        result.RetiredWind = RetiredWind;
        result.AcceptedThisFrame = Accepted;
        result.RejectedThisFrame = Rejected;
        result.WindCallsThisFrame = WindCalls;
        for (const auto &root : Roots) {
            result.ReservedEffects += root->Budget.Effects;
            result.ReservedWind += root->Budget.WindEffects;
            result.Pending += root->Simulator.PendingCount();
        }
        return result;
    }

    void Drain(Root &root) {
        auto events = root.Simulator.DrainTrace();
        root.Batch.insert(root.Batch.end(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
    }

    void Advance(Root &root, bool step, const World &world, std::vector<Collision> &collisions) {
        std::map<formula::ProjectileId, formula::Vec2> old;
        for (const auto &projectile : root.Simulator.Projectiles())
            old.emplace(projectile.Id, projectile.Position);
        root.Simulator.AdvanceTo(root.Simulator.Now() + (step ? 1 : 0));
        Drain(root);

        // Expiry wins at the boundary, so the excluded final segment has no Hit.
        for (const auto &projectile : root.Simulator.Projectiles()) {
            const auto previous = old.find(projectile.Id);
            const auto to = FormulaWorldAdapter::Offset(root.Anchor, projectile.Position);
            const auto from = previous == old.end() ? to : FormulaWorldAdapter::Offset(root.Anchor, previous->second);
            auto contact = FormulaWorldAdapter::Sweep(world, from, to);
            if (contact.Kind != FormulaContactKind::None)
                collisions.push_back({&root, projectile.Id, std::move(contact)});
        }
    }

    void Resolve(std::vector<Collision> &collisions) {
        std::sort(collisions.begin(), collisions.end(), [](const Collision &a, const Collision &b) {
            return std::tuple(a.Contact.Fraction, a.Owner->Id, a.Projectile, a.Contact.Cell.Y, a.Contact.Cell.X)
                 < std::tuple(b.Contact.Fraction, b.Owner->Id, b.Projectile, b.Contact.Cell.Y, b.Contact.Cell.X);
        });
        for (const auto &collision : collisions) {
            auto &root = *collision.Owner;
            if (collision.Contact.Kind == FormulaContactKind::Solid) {
                const auto origin = root.Anchor.Cell();
                const auto local = collision.Contact.Position.RelativeTo(origin);
                const auto anchor = root.Anchor.RelativeTo(origin);
                if (!root.Simulator.Hit(collision.Projectile, {local[0] - anchor[0], local[1] - anchor[1]}))
                    throw std::logic_error("collision references a retired formula projectile");
            } else {
                if (!root.Simulator.DiscardProjectile(collision.Projectile))
                    throw std::logic_error("collision references a retired formula projectile");
                Record(root.Id, root.Simulator.Now(), collision.Contact.Kind == FormulaContactKind::NotReady ? "NotReady" : "OutOfSimulation");
            }
            Drain(root);
        }
    }

    void Commit(Root &root, World &world) {
        // Analysis has validated every event. Internal errors propagate, including
        // after earlier effects committed; the host must not resume this session.
        for (const auto &event : root.Batch) {
            Record(root.Id, event.At, formula::FormatTrace(event));
            const auto position = FormulaWorldAdapter::Offset(root.Anchor, event.Position);
            if (const auto *dig = std::get_if<formula::DigArea>(&event.Data)) {
                const auto result = FormulaWorldAdapter::Dig(world, position, dig->Radius);
                Record(root.Id, event.At, "Dig applied=" + std::to_string(result.Applied) + " blocked=" + std::to_string(result.Blocked)
                    + " notReady=" + std::to_string(result.NotReady));
            } else if (const auto *wind = std::get_if<formula::WindImpulse>(&event.Data)) {
                const auto result = FormulaWorldAdapter::Wind(world, position, wind->Radius, wind->Velocity);
                if (result == FormulaWindResult::Applied)
                    ++WindCalls;
                Record(root.Id, event.At, FormulaWorldAdapter::WindResultName(result));
            }
        }
        root.Batch.clear();
    }

    void Refresh() {
        for (auto it = Roots.begin(); it != Roots.end();) {
            if ((*it)->Simulator.IsIdle()) {
                RetiredWind += (*it)->Budget.WindEffects;
                it = Roots.erase(it);
            } else {
                ++it;
            }
        }
        Projectiles.clear();
        for (const auto &root : Roots)
            for (const auto &projectile : root->Simulator.Projectiles())
                Projectiles.push_back({Epoch, root->Id, projectile.Id, root->Version,
                    FormulaWorldAdapter::Offset(root->Anchor, projectile.Position), FormulaWorldAdapter::BoltRadius,
                    projectile.Damage, projectile.Velocity});
    }
};

FormulaRuntime::FormulaRuntime() : Data(std::make_unique<State>()) {}
FormulaRuntime::~FormulaRuntime() = default;

bool FormulaRuntime::Apply(const formula::Formula &description) {
    if (Data->Version == UINT64_MAX)
        throw std::overflow_error("formula program version exhausted");
    const auto result = formula::validation::Analyze(description);
    if (!result) {
        Data->Error = result.Errors.empty() ? "formula analysis failed" : result.Errors.front().Field + ": " + result.Errors.front().Message;
        return false;
    }
    Data->Active = description;
    Data->ActiveBudget = result.Budget;
    ++Data->Version;
    Data->Error.clear();
    return true;
}

const formula::Formula &FormulaRuntime::ActiveFormula() const { return Data->Active; }
uint64_t FormulaRuntime::Version() const { return Data->Version; }
uint64_t FormulaRuntime::LastActivationId() const { return Data->NextCast - 1; }
const std::string &FormulaRuntime::LastError() const { return Data->Error; }
uint64_t FormulaRuntime::WorldEpoch() const { return Data->Epoch; }

void FormulaRuntime::BeginFrame() {
    Data->RetiredWind = 0;
    Data->Accepted = Data->Rejected = Data->WindCalls = 0;
}

bool FormulaRuntime::Cast(World &world, WorldPosition anchor, double angleDegrees) {
    if (world.GetEnvironment().IsWaitingForBudget())
        return Data->Reject("WaitingForBudget: activation requires a committed material step");
    if (!std::isfinite(angleDegrees))
        return Data->Reject("InvalidAim: angle must be finite");
    const auto stats = Data->Statistics();
    if (stats.AcceptedThisFrame >= MaxActivations)
        return Data->Reject("ActivationLimit: at most 8 activations per frame");
    if (stats.Roots >= MaxRoots)
        return Data->Reject("RootLimit: at most 16 running activations");
    if (stats.ReservedEffects + Data->ActiveBudget.Effects > MaxEffects)
        return Data->Reject("EffectLimit: session reservation exceeds 256");
    if (stats.ReservedWind + stats.RetiredWind + Data->ActiveBudget.WindEffects > MaxWind)
        return Data->Reject("WindLimit: frame reservation exceeds 64");

    if (Data->NextCast == UINT64_MAX)
        throw std::overflow_error("formula activation identity exhausted");
    anchor.Normalize();
    auto root = std::make_unique<Root>();
    root->Id = Data->NextCast++;
    root->Version = Data->Version;
    root->Anchor = anchor;
    root->Budget = Data->ActiveBudget;
    root->Simulator.Cast(Data->Active, {}, angleDegrees);
    auto &active = *root;
    Data->Roots.push_back(std::move(root));
    ++Data->Accepted;
    std::vector<Collision> collisions;
    Data->Advance(active, false, world, collisions);
    Data->Resolve(collisions);
    Data->Commit(active, world);
    Data->Refresh();
    Data->Error.clear();
    return true;
}

void FormulaRuntime::Step(World &world) {
    if (world.GetEnvironment().IsWaitingForBudget())
        throw std::logic_error("formula step requires a committed environment step");
    std::vector<Collision> collisions;
    // All sweeps see the same committed map, before any root's Dig is applied.
    for (auto &root : Data->Roots)
        Data->Advance(*root, true, world, collisions);
    Data->Resolve(collisions);
    for (auto &root : Data->Roots)
        Data->Commit(*root, world);
    Data->Refresh();
}

void FormulaRuntime::Clear() {
    for (const auto &root : Data->Roots)
        Data->RetiredWind += root->Budget.WindEffects;
    Data->Roots.clear();
    Data->Projectiles.clear();
    Data->Diagnostics.clear();
    Data->Error.clear();
}

void FormulaRuntime::OnWorldReplaced() {
    Clear();
    if (Data->Epoch == UINT64_MAX)
        throw std::overflow_error("formula diagnostic epoch exhausted");
    ++Data->Epoch;
}

const std::vector<FormulaProjectile> &FormulaRuntime::Projectiles() const { return Data->Projectiles; }
const std::deque<FormulaRuntimeDiagnostic> &FormulaRuntime::Diagnostics() const { return Data->Diagnostics; }
FormulaRuntimeStats FormulaRuntime::Stats() const { return Data->Statistics(); }
} // namespace pixel
