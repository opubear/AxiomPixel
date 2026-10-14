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
