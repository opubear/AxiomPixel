#pragma once
#include "Formula/Process/Program.h"
#include <array>
#include <vector>

namespace pixel::formula::process {
struct Cells { double Value = 0; };
struct Speed { double Value = 0; };
struct Duration { std::uint64_t Value = 0; };
using Value = std::variant<bool, std::int64_t, double, Cells, Speed, Duration, Effect, Outcome>;
enum class Type { Bool, Integer, Number, Cells, Speed, Duration, Effect, Outcome };
enum class ExpressionKind {
    Literal, Local, Inventory, Outcome, Add, Less, Equal, Not, Negate,
    Cells, Speed, Ticks, Dig, Wind, Bolt, DamageAdd, DamageMultiply, SpeedMultiply
};
struct Expression {
    ExpressionKind Kind = ExpressionKind::Literal;
    Type Result = Type::Bool;
    SourceSpan Source;
    Value Literal = false;
    std::array<std::size_t, 3> Arguments{};
    std::size_t Slot = 0, Depth = 1;
};
enum class InstructionKind { Assign, Emit, Branch, Jump, Wait, Await, Stop, Complete };
struct Instruction {
    InstructionKind Kind;
    SourceSpan Source;
    std::size_t Expression = 0, Target = 0, Slot = 0;
    AwaitKind Await = AwaitKind::Result;
};
struct ArtifactData {
    ProgramIdentity Identity;
    Capabilities Required = 0;
    std::string Source;
    std::vector<Expression> Expressions;
    std::vector<Instruction> Instructions;
};
std::string ContentSha256(std::string_view text);
} // namespace pixel::formula::process
