#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pixel::formula {
using Tick = std::uint64_t;
struct Node;
struct FormulaAccess;

enum class NodeKind {
    Empty, Bolt, Wind, Dig, Sequence, AddDamage, MultiplyDamage,
    MultiplySpeed, Delay, Repeat, Fan, OnHit
};

// A copied parameter record, with only the fields belonging to Kind in use.
// This is a public value type; the immutable implementation node stays private.
struct NodeValues {
    double Damage = 10;
    double Speed = 60;
    Tick Lifetime = 120;
    double Radius = 3;
    double Impulse = 120;
    double Amount = 5;
    double Factor = 2;
    Tick Ticks = 30;
    std::uint32_t Count = 3;
    Tick Interval = 12;
    double SpacingDegrees = 10;
};

const char *NodeKindName(NodeKind kind);

// A value is an immutable description, never a live projectile. Shared bindings
// remain unchanged when a new modifier/decorator wraps the same description.
class Formula {
  public:
    Formula(); // empty(), the identity of Sequence
    static Formula Bolt(double damage = 10, double speed = 60, Tick lifetime = 120);
    static Formula Wind(double radius, double impulse);
    static Formula Dig(double radius);
    static Formula Sequence(std::vector<Formula> children);
    Formula AddDamage(double amount) const;
    Formula MultiplyDamage(double factor) const;
    Formula MultiplySpeed(double factor) const;
    Formula After(Tick ticks) const;
    Formula Repeated(std::uint32_t count, Tick interval) const;
    Formula Spread(std::uint32_t count, double spacingDegrees) const;
    Formula OnHit(const Formula &payload) const;

    NodeKind Kind() const;
    NodeValues Values() const;
    std::size_t ChildCount() const;
    // Children are shared immutable values. Throws out_of_range for a bad slot.
    Formula Child(std::size_t index) const;

  private:
    explicit Formula(std::shared_ptr<const Node> node);
    std::shared_ptr<const Node> Description;
    friend struct FormulaAccess;
};

struct SourceLocation {
    std::size_t Line = 1;
    std::size_t Column = 1;
};
class ParseError : public std::runtime_error {
  public:
    ParseError(SourceLocation location, const std::string &message);
    SourceLocation Location;
};

// program := { 'let' name '=' expression ';' } 'cast' expression ';' EOF
// Throws ParseError with a 1-based location. No execution occurs during parsing.
Formula Parse(std::string_view source);
// Canonical expression text, accepted by Parse when prefixed with "cast ".
std::string Format(const Formula &formula);
} // namespace pixel::formula
