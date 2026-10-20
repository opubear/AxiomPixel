#include "Formula/Formula.h"
#include "Node.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <utility>

namespace pixel::formula {
namespace {
void Finite(double value, const char *name) {
    if (!std::isfinite(value))
        throw std::invalid_argument(std::string(name) + " must be finite");
}
void Nonnegative(double value, const char *name) {
    Finite(value, name);
    if (value < 0)
        throw std::invalid_argument(std::string(name) + " must be nonnegative");
}
std::string Number(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return out.str();
}
template <class... T>
struct Overload : T... {
    using T::operator()...;
};
} // namespace

Formula::Formula() : Formula(std::make_shared<Node>(Node{EmptyNode{}})) {}
Formula::Formula(std::shared_ptr<const Node> node) : Description(std::move(node)) {}
Formula Formula::Bolt(double damage, double speed, Tick lifetime) {
    Nonnegative(damage, "damage");
    Nonnegative(speed, "speed");
    if (!lifetime)
        throw std::invalid_argument("lifetime must be positive");
    return Formula(std::make_shared<Node>(Node{BoltNode{damage, speed, lifetime}}));
}
Formula Formula::Wind(double radius, double impulse) {
    Nonnegative(radius, "radius");
    Finite(impulse, "impulse");
    return Formula(std::make_shared<Node>(Node{WindNode{radius, impulse}}));
}
Formula Formula::Dig(double radius) {
    Nonnegative(radius, "radius");
    return Formula(std::make_shared<Node>(Node{DigNode{radius}}));
}
Formula Formula::Sequence(std::vector<Formula> children) {
    for (const auto &child : children)
        FormulaAccess::Get(child);
    return Formula(std::make_shared<Node>(Node{SequenceNode{std::move(children)}}));
}
Formula Formula::AddDamage(double amount) const {
    FormulaAccess::Get(*this);
    Finite(amount, "amount");
    return Formula(std::make_shared<Node>(Node{ModifyNode{ModifierKind::AddDamage, amount, *this}}));
}
Formula Formula::MultiplyDamage(double factor) const {
    FormulaAccess::Get(*this);
    Nonnegative(factor, "factor");
    return Formula(std::make_shared<Node>(Node{ModifyNode{ModifierKind::MultiplyDamage, factor, *this}}));
}
Formula Formula::MultiplySpeed(double factor) const {
    FormulaAccess::Get(*this);
    Nonnegative(factor, "factor");
    return Formula(std::make_shared<Node>(Node{ModifyNode{ModifierKind::MultiplySpeed, factor, *this}}));
}
Formula Formula::After(Tick ticks) const {
    FormulaAccess::Get(*this);
    return Formula(std::make_shared<Node>(Node{DelayNode{ticks, *this}}));
}
Formula Formula::Repeated(std::uint32_t count, Tick interval) const {
    FormulaAccess::Get(*this);
    return Formula(std::make_shared<Node>(Node{RepeatNode{count, interval, *this}}));
}
Formula Formula::Spread(std::uint32_t count, double spacingDegrees) const {
    FormulaAccess::Get(*this);
    Finite(spacingDegrees, "spacing");
    return Formula(std::make_shared<Node>(Node{FanNode{count, spacingDegrees, *this}}));
}
Formula Formula::OnHit(const Formula &payload) const {
    FormulaAccess::Get(*this);
    FormulaAccess::Get(payload);
    return Formula(std::make_shared<Node>(Node{OnHitNode{*this, payload}}));
}
NodeKind Formula::Kind() const {
    return std::visit(Overload{
        [](const EmptyNode &) { return NodeKind::Empty; },
        [](const BoltNode &) { return NodeKind::Bolt; },
        [](const WindNode &) { return NodeKind::Wind; },
        [](const DigNode &) { return NodeKind::Dig; },
        [](const SequenceNode &) { return NodeKind::Sequence; },
        [](const ModifyNode &n) {
            switch (n.Kind) {
            case ModifierKind::AddDamage: return NodeKind::AddDamage;
            case ModifierKind::MultiplyDamage: return NodeKind::MultiplyDamage;
            case ModifierKind::MultiplySpeed: return NodeKind::MultiplySpeed;
            }
            throw std::logic_error("unknown modifier");
        },
        [](const DelayNode &) { return NodeKind::Delay; },
        [](const RepeatNode &) { return NodeKind::Repeat; },
        [](const FanNode &) { return NodeKind::Fan; },
        [](const OnHitNode &) { return NodeKind::OnHit; }
    }, FormulaAccess::Get(*this).Data);
}
NodeValues Formula::Values() const {
    NodeValues values;
    std::visit(Overload{
        [](const EmptyNode &) {},
        [&](const BoltNode &n) { values.Damage = n.Damage; values.Speed = n.Speed; values.Lifetime = n.Lifetime; },
        [&](const WindNode &n) { values.Radius = n.Radius; values.Impulse = n.Impulse; },
        [&](const DigNode &n) { values.Radius = n.Radius; },
        [](const SequenceNode &) {},
        [&](const ModifyNode &n) {
            if (n.Kind == ModifierKind::AddDamage) values.Amount = n.Value;
            else values.Factor = n.Value;
        },
        [&](const DelayNode &n) { values.Ticks = n.Ticks; },
        [&](const RepeatNode &n) { values.Count = n.Count; values.Interval = n.Interval; },
        [&](const FanNode &n) { values.Count = n.Count; values.SpacingDegrees = n.SpacingDegrees; },
        [](const OnHitNode &) {}
    }, FormulaAccess::Get(*this).Data);
    return values;
}
std::size_t Formula::ChildCount() const {
    return std::visit(Overload{
        [](const SequenceNode &n) { return n.Children.size(); },
        [](const ModifyNode &) { return std::size_t{1}; },
        [](const DelayNode &) { return std::size_t{1}; },
        [](const RepeatNode &) { return std::size_t{1}; },
        [](const FanNode &) { return std::size_t{1}; },
        [](const OnHitNode &) { return std::size_t{2}; },
        [](const auto &) { return std::size_t{0}; }
    }, FormulaAccess::Get(*this).Data);
}
Formula Formula::Child(std::size_t index) const {
    if (index >= ChildCount())
        throw std::out_of_range("formula child index");
    return std::visit(Overload{
        [&](const SequenceNode &n) { return n.Children[index]; },
        [](const ModifyNode &n) { return n.Child; },
        [](const DelayNode &n) { return n.Child; },
        [](const RepeatNode &n) { return n.Child; },
        [](const FanNode &n) { return n.Child; },
        [&](const OnHitNode &n) { return index == 0 ? n.Carrier : n.Payload; },
        [](const auto &) -> Formula { throw std::out_of_range("formula has no children"); }
    }, FormulaAccess::Get(*this).Data);
}
const char *NodeKindName(NodeKind kind) {
    switch (kind) {
    case NodeKind::Empty: return "Empty";
    case NodeKind::Bolt: return "Bolt (base input)";
    case NodeKind::Wind: return "Wind";
    case NodeKind::Dig: return "Dig";
    case NodeKind::Sequence: return "Sequence";
    case NodeKind::AddDamage: return "Add damage";
    case NodeKind::MultiplyDamage: return "Multiply damage";
    case NodeKind::MultiplySpeed: return "Multiply speed";
    case NodeKind::Delay: return "Delay";
    case NodeKind::Repeat: return "Repeat";
    case NodeKind::Fan: return "Fan";
    case NodeKind::OnHit: return "On hit";
    }
    throw std::invalid_argument("unknown formula node kind");
}
const char *ModifierName(ModifierKind kind) {
    switch (kind) {
    case ModifierKind::AddDamage:
        return "damage_add";
    case ModifierKind::MultiplyDamage:
        return "damage_mul";
    case ModifierKind::MultiplySpeed:
        return "speed_mul";
    }
    throw std::logic_error("unknown modifier");
}
std::string Format(const Formula &formula) {
    return std::visit(Overload{
                          [](const EmptyNode &) { return std::string("empty()"); },
                          [](const BoltNode &n) { return "bolt(" + Number(n.Damage) + ", " + Number(n.Speed) + ", " + std::to_string(n.Lifetime) + ")"; },
                          [](const WindNode &n) { return "wind(" + Number(n.Radius) + ", " + Number(n.Impulse) + ")"; },
                          [](const DigNode &n) { return "dig(" + Number(n.Radius) + ")"; },
                          [](const SequenceNode &n) {
                              std::string result = "seq(";
                              for (std::size_t i = 0; i < n.Children.size(); ++i) {
                                  if (i)
                                      result += ", ";
                                  result += Format(n.Children[i]);
                              }
                              return result + ")";
                          },
                          [](const ModifyNode &n) { return std::string(ModifierName(n.Kind)) + "(" + Number(n.Value) + ", " + Format(n.Child) + ")"; },
                          [](const DelayNode &n) { return "delay(" + std::to_string(n.Ticks) + ", " + Format(n.Child) + ")"; },
                          [](const RepeatNode &n) { return "repeat(" + std::to_string(n.Count) + ", " + std::to_string(n.Interval) + ", " + Format(n.Child) + ")"; },
                          [](const FanNode &n) { return "fan(" + std::to_string(n.Count) + ", " + Number(n.SpacingDegrees) + ", " + Format(n.Child) + ")"; },
                          [](const OnHitNode &n) { return "on_hit(" + Format(n.Carrier) + ", " + Format(n.Payload) + ")"; }},
                      FormulaAccess::Get(formula).Data);
}
ParseError::ParseError(SourceLocation location, const std::string &message)
    : std::runtime_error(std::to_string(location.Line) + ":" + std::to_string(location.Column) + ": " + message), Location(location) {}
} // namespace pixel::formula
