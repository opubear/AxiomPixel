#include "FormulaAnalysis.h"
#include <algorithm>
#include <cmath>
#include <utility>

namespace pixel::formula::validation {
namespace {
struct ParameterValidator {
    std::vector<Diagnostic> Errors;

    void Error(const char *field, const char *message) {
        Errors.push_back({{}, field, message});
    }

    void Finite(double value, const char *field, bool nonnegative = false) {
        if (!std::isfinite(value))
            Error(field, "must be finite");
        else if (nonnegative && value < 0)
            Error(field, "must be nonnegative");
    }

    void Parameters(NodeKind kind, const NodeValues &values) {
        const auto &v = values;
        switch (kind) {
        case NodeKind::Bolt:
            Finite(v.Damage, "damage", true);
            Finite(v.Speed, "speed", true);
            if (v.Lifetime < 1 || v.Lifetime > 600)
                Error("lifetime", "must be between 1 and 600 ticks");
            break;
        case NodeKind::Wind:
            Finite(v.Impulse, "impulse");
            if (std::isfinite(v.Impulse) && std::abs(v.Impulse) > 600)
                Error("impulse", "absolute impulse must not exceed 600 cells/s");
            [[fallthrough]];
        case NodeKind::Dig:
            Finite(v.Radius, "radius", true);
            if (std::isfinite(v.Radius) && v.Radius > 16)
                Error("radius", "must not exceed 16 cells");
            break;
        case NodeKind::AddDamage: Finite(v.Amount, "amount"); break;
        case NodeKind::MultiplyDamage:
        case NodeKind::MultiplySpeed: Finite(v.Factor, "factor", true); break;
        case NodeKind::Delay:
            if (v.Ticks > 600)
                Error("ticks", "must not exceed 600 ticks");
            break;
        case NodeKind::Fan:
            Finite(v.SpacingDegrees, "spacing");
            [[fallthrough]];
        case NodeKind::Repeat:
            if (v.Count > 16)
                Error("count", "must not exceed 16");
            if (kind == NodeKind::Repeat && v.Interval > 600)
                Error("interval", "must not exceed 600 ticks");
            break;
        case NodeKind::Empty:
        case NodeKind::Sequence:
        case NodeKind::OnHit: break;
        default: Error("kind", "unknown node kind"); break;
        }
    }
};

struct LocatedFormula {
    Formula Description;
    std::vector<std::size_t> Path;

    LocatedFormula Child(std::size_t index) const {
        auto path = Path;
        path.push_back(index);
        return {Description.Child(index), std::move(path)};
    }
};

void ValidateTree(const LocatedFormula &node, std::size_t depth, AnalysisResult &result) {
    ++result.Budget.Nodes;
    result.Budget.Depth = std::max(result.Budget.Depth, depth);
    for (auto error : ValidateParameters(node.Description.Kind(), node.Description.Values())) {
        error.Path = node.Path;
        result.Errors.push_back(std::move(error));
    }
    for (std::size_t i = 0; i < node.Description.ChildCount(); ++i)
        ValidateTree(node.Child(i), depth + 1, result);
}

struct EvaluationContext {
    std::vector<LocatedFormula> Modifiers;
    std::vector<LocatedFormula> Payloads;
    double Angle = 0;
};

// Follow the existing evaluator's activation rules. Every carrier is assumed to
// hit at its last possible tick; payloads start one tick later with fresh scope.
// We stop at the first crossed bound, so hostile fan/repeat trees remain cheap.
struct WorkAnalyzer {
    AnalysisResult &Result;

    void Error(const LocatedFormula &node, const char *field, const char *message) {
        Result.Errors.push_back({node.Path, field, message});
    }

    bool ObserveTime(const LocatedFormula &node, Tick time) {
        Result.Budget.EndTick = std::max(Result.Budget.EndTick, time);
        if (time > MaximumEndTick) {
            Error(node, "endTick", "descendants must finish within 3600 ticks");
            return false;
        }
        return true;
    }

    void Bolt(const LocatedFormula &node, Tick at, const EvaluationContext &context) {
        const auto values = node.Description.Values();
        double damage = values.Damage;
        double speed = values.Speed;
        for (auto it = context.Modifiers.rbegin(); it != context.Modifiers.rend(); ++it) {
            const auto &modifier = *it;
            const auto kind = modifier.Description.Kind();
            const auto parameters = modifier.Description.Values();
            if (kind == NodeKind::AddDamage) {
                damage += parameters.Amount;
                if (std::isfinite(damage))
                    damage = std::max(0.0, damage);
            } else if (kind == NodeKind::MultiplyDamage)
                damage *= parameters.Factor;
            else
                speed *= parameters.Factor;
            if (!std::isfinite(damage) || !std::isfinite(speed)) {
                Error(modifier, "value", "evaluated projectile arithmetic must remain finite");
                return;
            }
        }
        if (speed > 600) {
            Error(node, "speed", "evaluated speed must not exceed 600 cells/s");
            return;
        }
        const Tick end = at + values.Lifetime;
        if (!ObserveTime(node, end))
            return;
        for (const auto &payload : context.Payloads) {
            Evaluate(payload, end, {{}, {}, context.Angle});
            if (!Result.Errors.empty())
                return;
        }
    }

    void Evaluate(const LocatedFormula &node, Tick at, EvaluationContext context) {
        if (!Result.Errors.empty())
            return;
        if (++Result.Budget.NodeVisits > MaximumVisits) {
            Error(node, "nodeVisits", "activation must not exceed 4096 node visits");
            return;
        }
        if (!ObserveTime(node, at))
            return;
        const auto kind = node.Description.Kind();
        if (kind == NodeKind::Bolt || kind == NodeKind::Wind || kind == NodeKind::Dig) {
            if (++Result.Budget.Effects > MaximumEffects) {
                Error(node, "effects", "activation and all hit descendants must not exceed 64 effects");
                return;
            }
            if (kind == NodeKind::Wind)
                ++Result.Budget.WindEffects;
        }

        const auto v = node.Description.Values();
        switch (kind) {
        case NodeKind::Bolt: Bolt(node, at, context); break;
        case NodeKind::Empty:
        case NodeKind::Wind:
        case NodeKind::Dig: break;
        case NodeKind::Sequence:
            for (std::size_t i = 0; i < node.Description.ChildCount(); ++i)
                Evaluate(node.Child(i), at, context);
            break;
        case NodeKind::AddDamage:
        case NodeKind::MultiplyDamage:
        case NodeKind::MultiplySpeed:
            context.Modifiers.push_back(node);
            Evaluate(node.Child(0), at, std::move(context));
            break;
        case NodeKind::OnHit:
            context.Payloads.push_back(node.Child(1));
            Evaluate(node.Child(0), at, std::move(context));
            break;
        case NodeKind::Delay:
            Evaluate(node.Child(0), at + v.Ticks, std::move(context));
            break;
        case NodeKind::Repeat:
            for (std::uint32_t i = 0; i < v.Count; ++i)
                Evaluate(node.Child(0), at + static_cast<Tick>(i) * v.Interval, context);
            break;
        case NodeKind::Fan:
            for (std::uint32_t i = 0; i < v.Count; ++i) {
                auto branch = context;
                const double offset = (static_cast<double>(i) - (v.Count - 1.0) / 2) * v.SpacingDegrees;
                branch.Angle += offset;
                if (!std::isfinite(offset) || !std::isfinite(branch.Angle)) {
                    Error(node, "spacing", "evaluated fan angle must remain finite");
                    return;
                }
                Evaluate(node.Child(0), at, std::move(branch));
            }
            break;
        }
    }
};
} // namespace

const char *ShapeError(const Formula &formula) {
    std::vector<std::pair<Formula, std::size_t>> pending{{formula, 1}};
    std::size_t count = 0;
    while (!pending.empty()) {
        auto [node, depth] = std::move(pending.back());
        pending.pop_back();
        if (++count > MaximumNodes)
            return "nodes";
        if (depth > MaximumDepth)
            return "depth";
        if (node.ChildCount() > MaximumNodes - count - pending.size())
            return "nodes";
        for (std::size_t i = 0; i < node.ChildCount(); ++i)
            pending.emplace_back(node.Child(i), depth + 1);
    }
    return nullptr;
}

std::vector<Diagnostic> ValidateParameters(NodeKind kind, const NodeValues &values) {
    ParameterValidator validator;
    validator.Parameters(kind, values);
    return std::move(validator.Errors);
}

AnalysisResult Analyze(const Formula &formula) {
    AnalysisResult result;
    if (const auto *field = ShapeError(formula)) {
        result.Errors.push_back({{}, field, "formula exceeds gameplay tree limits"});
        return result;
    }
    const LocatedFormula root{formula, {}};
    ValidateTree(root, 1, result);
    if (result.Errors.empty())
        WorkAnalyzer{result}.Evaluate(root, 0, {});
    return result;
}
} // namespace pixel::formula::validation
