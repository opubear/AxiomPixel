#pragma once
#include "Formula/Formula.h"
#include <variant>

namespace pixel::formula {
struct EmptyNode {};
struct BoltNode {
    double Damage, Speed;
    Tick Lifetime;
};
struct WindNode {
    double Radius, Impulse;
};
struct DigNode {
    double Radius;
};
struct SequenceNode {
    std::vector<Formula> Children;
};
enum class ModifierKind { AddDamage,
                          MultiplyDamage,
                          MultiplySpeed };
struct ModifyNode {
    ModifierKind Kind;
    double Value;
    Formula Child;
};
struct DelayNode {
    Tick Ticks;
    Formula Child;
};
struct RepeatNode {
    std::uint32_t Count;
    Tick Interval;
    Formula Child;
};
struct FanNode {
    std::uint32_t Count;
    double SpacingDegrees;
    Formula Child;
};
struct OnHitNode {
    Formula Carrier, Payload;
};
struct Node {
    std::variant<EmptyNode, BoltNode, WindNode, DigNode, SequenceNode, ModifyNode,
                 DelayNode, RepeatNode, FanNode, OnHitNode>
        Data;
};
struct FormulaAccess {
    static const Node &Get(const Formula &formula) {
        if (!formula.Description)
            throw std::invalid_argument("cannot use a moved-from Formula");
        return *formula.Description;
    }
};
const char *ModifierName(ModifierKind kind);
} // namespace pixel::formula
