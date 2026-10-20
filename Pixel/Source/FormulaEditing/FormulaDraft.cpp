#include "FormulaDraft.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace pixel::formula::editing {
namespace {
constexpr std::size_t MaximumNodes = 64;
constexpr std::size_t MaximumDepth = 16;
constexpr std::size_t MaximumEffects = 64;
constexpr std::size_t MaximumVisits = 4096;
constexpr Tick MaximumEndTick = 3600;

std::size_t FixedChildren(NodeKind kind) {
    switch (kind) {
    case NodeKind::AddDamage:
    case NodeKind::MultiplyDamage:
    case NodeKind::MultiplySpeed:
    case NodeKind::Delay:
    case NodeKind::Repeat:
    case NodeKind::Fan: return 1;
    case NodeKind::OnHit: return 2;
    default: return 0;
    }
}

struct Location {
    std::unique_ptr<DraftNode> *Owner = nullptr;
    DraftNode *Parent = nullptr;
    std::size_t Index = 0;
};

Location Locate(std::unique_ptr<DraftNode> &owner, NodeId id) {
    struct Pending {
        Location Where;
        std::size_t Depth;
    };
    std::vector<Pending> pending{{{&owner, nullptr, 0}, 1}};
    std::size_t visited = 0;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        auto *node = current.Where.Owner->get();
        if (!node)
            continue;
        if (++visited > MaximumNodes || current.Depth > MaximumDepth)
            return {};
        if (node->Id == id)
            return current.Where;
        if (node->Children.size() > MaximumNodes - visited - pending.size())
            return {};
        for (std::size_t i = node->Children.size(); i > 0; --i)
            pending.push_back({{&node->Children[i - 1], node, i - 1}, current.Depth + 1});
    }
    return {};
}

const DraftNode *FindNode(const DraftNode &node, NodeId id) {
    std::vector<std::pair<const DraftNode *, std::size_t>> pending{{&node, 1}};
    std::size_t visited = 0;
    while (!pending.empty()) {
        const auto [current, depth] = pending.back();
        pending.pop_back();
        if (!current)
            continue;
        if (++visited > MaximumNodes || depth > MaximumDepth)
            return nullptr;
        if (current->Id == id)
            return current;
        if (current->Children.size() > MaximumNodes - visited - pending.size())
            return nullptr;
        for (auto child = current->Children.rbegin(); child != current->Children.rend(); ++child)
            pending.emplace_back(child->get(), depth + 1);
    }
    return nullptr;
}

struct Shape {
    std::size_t Nodes = 0;
    std::size_t Depth = 0;
};

// Measure a projected replacement without visiting the subtree it removes.
// This permits Replace to repair oversized drafts created by direct Root edits.
std::optional<Shape> Measure(const DraftNode &root, const DraftNode *replaced = nullptr,
                             Shape replacement = {}) {
    Shape shape;
    std::vector<std::pair<const DraftNode *, std::size_t>> pending{{&root, 1}};
    while (!pending.empty()) {
        const auto [node, depth] = pending.back();
        pending.pop_back();
        if (!node)
            return {};
        if (node == replaced) {
            if (replacement.Nodes > MaximumNodes - shape.Nodes || depth - 1 + replacement.Depth > MaximumDepth)
                return {};
            shape.Nodes += replacement.Nodes;
            shape.Depth = std::max(shape.Depth, depth - 1 + replacement.Depth);
            if (pending.size() > MaximumNodes - shape.Nodes)
                return {};
            continue;
        }
        if (++shape.Nodes > MaximumNodes || depth > MaximumDepth)
            return {};
        shape.Depth = std::max(shape.Depth, depth);
        if (pending.size() > MaximumNodes - shape.Nodes ||
            node->Children.size() > MaximumNodes - shape.Nodes - pending.size())
            return {};
        for (const auto &child : node->Children)
            pending.emplace_back(child.get(), depth + 1);
    }
    return shape;
}

std::optional<Shape> NewShape(NodeKind kind) {
    if (kind < NodeKind::Empty || kind > NodeKind::OnHit)
        return {};
    const auto children = FixedChildren(kind);
    return Shape{1 + children, children ? 2u : 1u};
}

const char *FormulaShapeError(const Formula &formula) {
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

struct Validator {
    CompileResult &Result;
    std::set<NodeId> Seen;

    void Error(const DraftNode &node, const char *field, const char *message) {
        Result.Errors.push_back({node.Id, field, message});
    }

    void Finite(const DraftNode &node, double value, const char *field, bool nonnegative = false) {
        if (!std::isfinite(value))
            Error(node, field, "must be finite");
        else if (nonnegative && value < 0)
            Error(node, field, "must be nonnegative");
    }

    void Parameters(const DraftNode &node) {
        const auto &v = node.Values;
        switch (node.Kind) {
        case NodeKind::Bolt:
            Finite(node, v.Damage, "damage", true);
            Finite(node, v.Speed, "speed", true);
            if (v.Lifetime < 1 || v.Lifetime > 600)
                Error(node, "lifetime", "must be between 1 and 600 ticks");
            break;
        case NodeKind::Wind:
            Finite(node, v.Impulse, "impulse");
            if (std::isfinite(v.Impulse) && std::abs(v.Impulse) > 600)
                Error(node, "impulse", "absolute impulse must not exceed 600 cells/s");
            [[fallthrough]];
        case NodeKind::Dig:
            Finite(node, v.Radius, "radius", true);
            if (std::isfinite(v.Radius) && v.Radius > 16)
                Error(node, "radius", "must not exceed 16 cells");
            break;
        case NodeKind::AddDamage: Finite(node, v.Amount, "amount"); break;
        case NodeKind::MultiplyDamage:
        case NodeKind::MultiplySpeed: Finite(node, v.Factor, "factor", true); break;
        case NodeKind::Delay:
            if (v.Ticks > 600)
                Error(node, "ticks", "must not exceed 600 ticks");
            break;
        case NodeKind::Fan:
            Finite(node, v.SpacingDegrees, "spacing");
            [[fallthrough]];
        case NodeKind::Repeat:
            if (v.Count > 16)
                Error(node, "count", "must not exceed 16");
            if (node.Kind == NodeKind::Repeat && v.Interval > 600)
                Error(node, "interval", "must not exceed 600 ticks");
            break;
        case NodeKind::Empty:
        case NodeKind::Sequence:
        case NodeKind::OnHit: break;
        default: Error(node, "kind", "unknown node kind"); break;
        }
    }

    void Visit(const DraftNode &node, std::size_t depth) {
        auto &budget = Result.Budget;
        ++budget.Nodes;
        budget.Depth = std::max(budget.Depth, depth);
        if (budget.Nodes > MaximumNodes) {
            Error(node, "nodes", "draft must not exceed 64 nodes");
            return;
        }
        if (depth > MaximumDepth) {
            Error(node, "depth", "draft must not exceed depth 16");
            return;
        }
        if (node.Id == 0 || !Seen.insert(node.Id).second)
            Error(node, "id", "node ID must be nonzero and unique");
        if (node.Kind != NodeKind::Sequence && node.Children.size() != FixedChildren(node.Kind))
            Error(node, "children", "incorrect number of child slots");
        Parameters(node);
        for (const auto &child : node.Children) {
            if (!child)
                Error(node, "children", "child slot must contain a node");
            else
                Visit(*child, depth + 1);
            if (budget.Nodes > MaximumNodes)
                return;
        }
    }
};

Formula Build(const DraftNode &node) {
    const auto &v = node.Values;
    switch (node.Kind) {
    case NodeKind::Empty: return Formula{};
    case NodeKind::Bolt: return Formula::Bolt(v.Damage, v.Speed, v.Lifetime);
    case NodeKind::Wind: return Formula::Wind(v.Radius, v.Impulse);
    case NodeKind::Dig: return Formula::Dig(v.Radius);
    case NodeKind::Sequence: {
        std::vector<Formula> children;
        children.reserve(node.Children.size());
        for (const auto &child : node.Children)
            children.push_back(Build(*child));
        return Formula::Sequence(std::move(children));
    }
    case NodeKind::AddDamage: return Build(*node.Children[0]).AddDamage(v.Amount);
    case NodeKind::MultiplyDamage: return Build(*node.Children[0]).MultiplyDamage(v.Factor);
    case NodeKind::MultiplySpeed: return Build(*node.Children[0]).MultiplySpeed(v.Factor);
    case NodeKind::Delay: return Build(*node.Children[0]).After(v.Ticks);
    case NodeKind::Repeat: return Build(*node.Children[0]).Repeated(v.Count, v.Interval);
    case NodeKind::Fan: return Build(*node.Children[0]).Spread(v.Count, v.SpacingDegrees);
    case NodeKind::OnHit: return Build(*node.Children[0]).OnHit(Build(*node.Children[1]));
    }
    throw std::invalid_argument("unknown formula node kind");
}

struct EvaluationContext {
    std::vector<const DraftNode *> Modifiers;
    std::vector<const DraftNode *> Payloads;
    double Angle = 0;
};

// Follow the existing evaluator's activation rules. Every carrier is assumed to
// hit at its last possible tick; payloads start one tick later with fresh scope.
// We stop at the first crossed bound, so hostile fan/repeat trees remain cheap.
struct WorkAnalyzer {
    CompileResult &Result;

    void Error(const DraftNode &node, const char *field, const char *message) {
        Result.Errors.push_back({node.Id, field, message});
    }

    bool ObserveTime(const DraftNode &node, Tick time) {
        Result.Budget.EndTick = std::max(Result.Budget.EndTick, time);
        if (time > MaximumEndTick) {
            Error(node, "endTick", "descendants must finish within 3600 ticks");
            return false;
        }
        return true;
    }

    void Bolt(const DraftNode &node, Tick at, const EvaluationContext &context) {
        double damage = node.Values.Damage;
        double speed = node.Values.Speed;
        for (auto it = context.Modifiers.rbegin(); it != context.Modifiers.rend(); ++it) {
            const auto &modifier = **it;
            if (modifier.Kind == NodeKind::AddDamage) {
                damage += modifier.Values.Amount;
                if (std::isfinite(damage))
                    damage = std::max(0.0, damage);
            } else if (modifier.Kind == NodeKind::MultiplyDamage)
                damage *= modifier.Values.Factor;
            else
                speed *= modifier.Values.Factor;
            if (!std::isfinite(damage) || !std::isfinite(speed)) {
                Error(modifier, "value", "evaluated projectile arithmetic must remain finite");
                return;
            }
        }
        if (speed > 600) {
            Error(node, "speed", "evaluated speed must not exceed 600 cells/s");
            return;
        }
        const Tick end = at + node.Values.Lifetime;
        if (!ObserveTime(node, end))
            return;
        for (const auto *payload : context.Payloads) {
            Evaluate(*payload, end, {{}, {}, context.Angle});
            if (!Result.Errors.empty())
                return;
        }
    }

    void Evaluate(const DraftNode &node, Tick at, EvaluationContext context) {
        if (!Result.Errors.empty())
            return;
        if (++Result.Budget.NodeVisits > MaximumVisits) {
            Error(node, "nodeVisits", "activation must not exceed 4096 node visits");
            return;
        }
        if (!ObserveTime(node, at))
            return;
        if (node.Kind == NodeKind::Bolt || node.Kind == NodeKind::Wind || node.Kind == NodeKind::Dig) {
            if (++Result.Budget.Effects > MaximumEffects) {
                Error(node, "effects", "activation and all hit descendants must not exceed 64 effects");
                return;
            }
            if (node.Kind == NodeKind::Wind)
                ++Result.Budget.WindEffects;
        }

        const auto &v = node.Values;
        switch (node.Kind) {
        case NodeKind::Bolt: Bolt(node, at, context); break;
        case NodeKind::Empty:
        case NodeKind::Wind:
        case NodeKind::Dig: break;
        case NodeKind::Sequence:
            for (const auto &child : node.Children)
                Evaluate(*child, at, context);
            break;
        case NodeKind::AddDamage:
        case NodeKind::MultiplyDamage:
        case NodeKind::MultiplySpeed:
            context.Modifiers.push_back(&node);
            Evaluate(*node.Children[0], at, std::move(context));
            break;
        case NodeKind::OnHit:
            context.Payloads.push_back(node.Children[1].get());
            Evaluate(*node.Children[0], at, std::move(context));
            break;
        case NodeKind::Delay:
            Evaluate(*node.Children[0], at + v.Ticks, std::move(context));
            break;
        case NodeKind::Repeat:
            for (std::uint32_t i = 0; i < v.Count; ++i)
                Evaluate(*node.Children[0], at + static_cast<Tick>(i) * v.Interval, context);
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
                Evaluate(*node.Children[0], at, std::move(branch));
            }
            break;
        }
    }
};
} // namespace

Draft::Draft() : Tree(NewNode(NodeKind::Bolt)) {}
DraftNode &Draft::Root() { return *Tree; }
const DraftNode &Draft::Root() const { return *Tree; }
DraftNode *Draft::Find(NodeId id) {
    const auto found = Locate(Tree, id);
    return found.Owner ? found.Owner->get() : nullptr;
}
const DraftNode *Draft::Find(NodeId id) const { return FindNode(*Tree, id); }

std::unique_ptr<DraftNode> Draft::NewNode(NodeKind kind) {
    if (NextId == std::numeric_limits<NodeId>::max())
        throw std::overflow_error("draft node IDs exhausted");
    auto result = std::make_unique<DraftNode>(DraftNode{NextId++, kind, {}, {}});
    for (std::size_t i = 0; i < FixedChildren(kind); ++i)
        result->Children.push_back(NewNode(i == 0 ? NodeKind::Bolt : NodeKind::Empty));
    return result;
}

std::unique_ptr<DraftNode> Draft::CopyNode(const DraftNode &node) {
    auto result = NewNode(NodeKind::Empty);
    result->Kind = node.Kind;
    result->Values = node.Values;
    for (const auto &child : node.Children)
        result->Children.push_back(CopyNode(*child));
    return result;
}

std::unique_ptr<DraftNode> Draft::Import(const Formula &formula) {
    auto result = NewNode(NodeKind::Empty);
    result->Kind = formula.Kind();
    result->Values = formula.Values();
    for (std::size_t i = 0; i < formula.ChildCount(); ++i)
        result->Children.push_back(Import(formula.Child(i)));
    return result;
}
void Draft::FromFormula(const Formula &formula) {
    if (FormulaShapeError(formula))
        throw std::invalid_argument("formula exceeds 64 nodes or depth 16");
    Tree = Import(formula);
}

NodeId Draft::AddChild(NodeId parent, NodeKind kind) {
    if (!Measure(*Tree))
        return 0;
    auto *node = Find(parent);
    if (!node || node->Kind != NodeKind::Sequence)
        return 0;
    const auto subtree = Measure(*node);
    const auto added = NewShape(kind);
    if (!added || !Measure(*Tree, node, {subtree->Nodes + added->Nodes, std::max(subtree->Depth, added->Depth + 1)}))
        return 0;
    auto child = NewNode(kind);
    const auto id = child->Id;
    node->Children.push_back(std::move(child));
    return id;
}

NodeId Draft::Replace(NodeId id, NodeKind kind) {
    auto found = Locate(Tree, id);
    if (!found.Owner)
        return 0;
    const auto replacementShape = NewShape(kind);
    if (!replacementShape || !Measure(*Tree, found.Owner->get(), *replacementShape))
        return 0;
    auto replacement = NewNode(kind);
    const auto selection = found.Parent ? found.Parent->Id : replacement->Id;
    *found.Owner = std::move(replacement);
    return selection;
}

NodeId Draft::Wrap(NodeId id, NodeKind kind) {
    if (!Measure(*Tree))
        return 0;
    auto found = Locate(Tree, id);
    if (!found.Owner || (kind != NodeKind::Sequence && FixedChildren(kind) == 0))
        return 0;
    const auto subtree = Measure(**found.Owner);
    const auto wrapper = NewShape(kind);
    const auto addedNodes = kind == NodeKind::OnHit ? 2u : 1u;
    if (!wrapper || !Measure(*Tree, found.Owner->get(), {subtree->Nodes + addedNodes, subtree->Depth + 1}))
        return 0;
    auto wrapped = NewNode(kind);
    if (kind == NodeKind::Sequence)
        wrapped->Children.push_back(std::move(*found.Owner));
    else
        wrapped->Children[0] = std::move(*found.Owner);
    const auto selection = wrapped->Id;
    *found.Owner = std::move(wrapped);
    return selection;
}

NodeId Draft::Unwrap(NodeId id, std::size_t child) {
    auto found = Locate(Tree, id);
    if (!found.Owner || child >= (*found.Owner)->Children.size())
        return 0;
    if ((*found.Owner)->Kind == NodeKind::Sequence && (*found.Owner)->Children.size() != 1)
        return 0;
    auto replacement = std::move((*found.Owner)->Children[child]);
    if (!replacement)
        return 0;
    const auto selection = replacement->Id;
    *found.Owner = std::move(replacement);
    return selection;
}

NodeId Draft::Delete(NodeId id) {
    auto found = Locate(Tree, id);
    if (!found.Owner)
        return 0;
    if (!found.Parent) {
        Tree = NewNode(NodeKind::Empty);
        return Tree->Id;
    }
    const auto selection = found.Parent->Id;
    if (found.Parent->Kind == NodeKind::Sequence)
        found.Parent->Children.erase(found.Parent->Children.begin() + found.Index);
    else
        *found.Owner = NewNode(NodeKind::Empty);
    return selection;
}

NodeId Draft::Duplicate(NodeId id) {
    if (!Measure(*Tree))
        return 0;
    auto found = Locate(Tree, id);
    if (!found.Owner || (found.Parent && found.Parent->Kind != NodeKind::Sequence))
        return 0;
    const auto subtree = Measure(**found.Owner);
    const Shape duplicated{2 * subtree->Nodes + (found.Parent ? 0u : 1u),
                           subtree->Depth + (found.Parent ? 0u : 1u)};
    if (!Measure(*Tree, found.Owner->get(), duplicated))
        return 0;
    auto copy = CopyNode(**found.Owner);
    const auto selection = copy->Id;
    if (!found.Parent) {
        auto wrapper = NewNode(NodeKind::Sequence);
        wrapper->Children.push_back(std::move(Tree));
        wrapper->Children.push_back(std::move(copy));
        Tree = std::move(wrapper);
    } else {
        auto &children = found.Parent->Children;
        children.insert(children.begin() + found.Index + 1, std::move(copy));
    }
    return selection;
}

bool Draft::Move(NodeId id, int direction) {
    const auto found = Locate(Tree, id);
    if (!found.Owner || !found.Parent || found.Parent->Kind != NodeKind::Sequence || direction == 0)
        return false;
    auto &children = found.Parent->Children;
    if ((direction < 0 && found.Index == 0) || (direction > 0 && found.Index + 1 == children.size()))
        return false;
    const auto target = direction < 0 ? found.Index - 1 : found.Index + 1;
    std::swap(children[found.Index], children[target]);
    return true;
}

CompileResult Compile(const Draft &draft) {
    CompileResult result;
    Validator{result, {}}.Visit(draft.Root(), 1);
    if (!result.Errors.empty())
        return result;
    WorkAnalyzer{result}.Evaluate(draft.Root(), 0, {});
    if (result.Errors.empty())
        result.Description = Build(draft.Root());
    return result;
}

CompileResult Analyze(const Formula &formula) {
    if (const auto *field = FormulaShapeError(formula)) {
        CompileResult result;
        result.Errors.push_back({0, field, "formula exceeds gameplay tree limits"});
        return result;
    }
    Draft draft;
    draft.FromFormula(formula);
    auto result = Compile(draft);
    if (result)
        result.Description = formula;
    return result;
}
} // namespace pixel::formula::editing
