#include "../ProcessProgram.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>

namespace pixel::formula::process {
namespace {
struct CompileFailure { SourceSpan Source; std::string Message; };
struct Token { std::string_view Text; SourceSpan Source; };
bool Letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Numeric(Type type) { return type == Type::Integer || type == Type::Number; }
bool Reserved(std::string_view name) {
    constexpr std::array words{"process", "let", "var", "if", "else", "while", "emit", "wait",
        "await", "within", "stop", "result", "true", "false", "applied", "rejected", "hit",
        "expired", "cancelled", "wake", "timeout", "inventory", "outcome", "cells", "speed",
        "ticks", "dig", "wind", "bolt", "damage_add", "damage_mul", "speed_mul"};
    return std::find(words.begin(), words.end(), name) != words.end();
}
std::vector<Token> Lex(std::string_view source) {
    std::vector<Token> tokens;
    std::size_t at = 0, line = 1, column = 1;
    auto advance = [&] {
        if (source[at++] == '\n') { ++line; column = 1; }
        else ++column;
    };
    while (at < source.size()) {
        const auto c = source[at];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (source.substr(at, 2) == "//") {
            while (at < source.size() && source[at] != '\n') advance();
            continue;
        }
        SourceSpan span{at, 1, line, column};
        if (Letter(c)) {
            do { advance(); } while (at < source.size() && (Letter(source[at]) || Digit(source[at])));
            if (at - span.Offset > 64) throw CompileFailure{span, "identifier exceeds 64 bytes"};
        } else if (Digit(c)) {
            do { advance(); } while (at < source.size() && Digit(source[at]));
            if (at < source.size() && source[at] == '.') {
                advance();
                while (at < source.size() && Digit(source[at])) advance();
            }
        } else if (std::string_view("{}();,=+<!-").find(c) != std::string_view::npos) {
            advance();
            if (c == '=' && at < source.size() && source[at] == '=') advance();
        } else {
            throw CompileFailure{span, "unexpected character"};
        }
        span.Length = at - span.Offset;
        tokens.push_back({source.substr(span.Offset, span.Length), span});
    }
    tokens.push_back({{}, {source.size(), 0, line, column}});
    return tokens;
}
struct Binding { std::string_view Name; Type Kind; std::size_t Slot; bool Mutable; };
struct Builtin {
    std::string_view Name;
    ExpressionKind Kind;
    Type Result;
    std::array<Type, 3> Arguments{};
    std::size_t Count = 0;
    Capabilities Required = 0;
};
constexpr std::array Builtins{
    Builtin{"inventory", ExpressionKind::Inventory, Type::Integer, {}, 0, Mask(Capability::Inventory)},
    Builtin{"outcome", ExpressionKind::Outcome, Type::Outcome},
    Builtin{"cells", ExpressionKind::Cells, Type::Cells, {Type::Number}, 1},
    Builtin{"speed", ExpressionKind::Speed, Type::Speed, {Type::Number}, 1},
    Builtin{"ticks", ExpressionKind::Ticks, Type::Duration, {Type::Integer}, 1},
    Builtin{"dig", ExpressionKind::Dig, Type::Effect, {Type::Cells}, 1, Mask(Capability::Dig)},
    Builtin{"wind", ExpressionKind::Wind, Type::Effect, {Type::Cells, Type::Speed, Type::Speed}, 3, Mask(Capability::Wind)},
    Builtin{"bolt", ExpressionKind::Bolt, Type::Effect, {Type::Number, Type::Speed, Type::Duration}, 3, Mask(Capability::Bolt)},
    Builtin{"damage_add", ExpressionKind::DamageAdd, Type::Effect, {Type::Effect, Type::Number}, 2},
    Builtin{"damage_mul", ExpressionKind::DamageMultiply, Type::Effect, {Type::Effect, Type::Number}, 2},
    Builtin{"speed_mul", ExpressionKind::SpeedMultiply, Type::Effect, {Type::Effect, Type::Number}, 2}};
} // namespace

class Compiler {
  public:
    Compiler(std::string_view resource, std::string_view source, Capabilities available)
        : Data(std::make_shared<ArtifactData>()), Available(available) {
        if (resource.empty() || resource.size() > 256)
            throw CompileFailure{{}, "resource name must contain 1..256 bytes"};
        if (source.size() > 16384)
            throw CompileFailure{{}, "source exceeds 16384 bytes"};
        Data->Identity.Resource = resource;
        Data->Identity.ContentSha256 = ContentSha256(source);
        Data->Source = source;
        Tokens = Lex(Data->Source);
    }
    ProgramArtifact Run() {
        Expect("process");
        Expect("1");
        Expect(";");
        while (!Peek().Text.empty()) Statement();
        AddInstruction({InstructionKind::Complete, Peek().Source});
        return ProgramArtifact(std::move(Data));
    }

  private:
    const Token &Peek() const { return Tokens[Cursor]; }
    Token Take() { return Tokens[Cursor++]; }
    bool Accept(std::string_view text) {
        if (Peek().Text != text) return false;
        ++Cursor;
        return true;
    }
    Token Expect(std::string_view text) {
        if (Peek().Text != text) Fail(Peek().Source, "expected '" + std::string(text) + "'");
        return Take();
    }
    [[noreturn]] void Fail(SourceSpan source, std::string message) const {
        throw CompileFailure{source, std::move(message)};
    }
    void RequireType(std::size_t expression, Type expected, SourceSpan source, bool numeric = false) {
        const auto actual = Data->Expressions[expression].Result;
        if (actual != expected && !(numeric && expected == Type::Number && Numeric(actual)))
            Fail(source, "expression type does not match this operation");
    }
    void RequireCapability(Capabilities required, SourceSpan source) {
        if ((required & Available) != required) Fail(source, "host capability is unavailable");
        Data->Required |= required;
    }
    std::size_t AddExpression(Expression expression) {
        if (Data->Expressions.size() >= 1024 || expression.Depth > 24)
            Fail(expression.Source, "expression storage/depth limit exceeded");
        Data->Expressions.push_back(std::move(expression));
        return Data->Expressions.size() - 1;
    }
    std::size_t AddInstruction(Instruction instruction) {
        if (Data->Instructions.size() >= 512) Fail(instruction.Source, "instruction limit exceeded");
        Data->Instructions.push_back(instruction);
        return Data->Instructions.size() - 1;
    }
    const Binding &Lookup(const Token &name) const {
        const auto found = std::find_if(Bindings.begin(), Bindings.end(), [&](auto &b) { return b.Name == name.Text; });
        if (found == Bindings.end()) Fail(name.Source, "unknown binding '" + std::string(name.Text) + "'");
        return *found;
    }
    void Block() {
        const auto open = Expect("{");
        if (++BlockDepth > 24) Fail(open.Source, "block depth exceeds 24");
        const auto saved = Bindings.size();
        while (Peek().Text != "}") {
            if (Peek().Text.empty()) Fail(Peek().Source, "expected '}'");
            Statement();
        }
        Expect("}");
        Bindings.resize(saved);
        --BlockDepth;
    }
    void Declare(bool mutableBinding, SourceSpan source) {
        const auto name = Take();
        if (name.Text.empty() || !Letter(name.Text.front()) || Reserved(name.Text))
            Fail(name.Source, "expected a binding name");
        if (std::any_of(Bindings.begin(), Bindings.end(), [&](auto &b) { return b.Name == name.Text; }))
            Fail(name.Source, "binding shadowing is not supported");
        if (NextSlot == 16) Fail(name.Source, "program exceeds 16 local bindings");
        Expect("=");
        const auto value = ParseExpression();
        Expect(";");
        Bindings.push_back({name.Text, Data->Expressions[value].Result, NextSlot, mutableBinding});
        AddInstruction({InstructionKind::Assign, source, value, 0, NextSlot++});
    }
    void Conditional(SourceSpan source, bool loop) {
        const auto start = Data->Instructions.size();
        const auto condition = ParseExpression();
        RequireType(condition, Type::Bool, source);
        const auto branch = AddInstruction({InstructionKind::Branch, source, condition});
        Block();
        if (loop) AddInstruction({InstructionKind::Jump, source, 0, start});
        if (!loop && Accept("else")) {
            const auto jump = AddInstruction({InstructionKind::Jump, source});
            Data->Instructions[branch].Target = Data->Instructions.size();
            Block();
            Data->Instructions[jump].Target = Data->Instructions.size();
        } else Data->Instructions[branch].Target = Data->Instructions.size();
    }
    void Await(SourceSpan source) {
        const auto kind = Take();
        AwaitKind await;
        if (kind.Text == "result") await = AwaitKind::Result;
        else if (kind.Text == "hit") await = AwaitKind::Hit;
        else if (kind.Text == "wake") {
            await = AwaitKind::Wake;
            RequireCapability(Mask(Capability::Wake), kind.Source);
        } else Fail(kind.Source, "expected result, hit or wake");
        Expect("within");
        const auto timeout = ParseExpression();
        RequireType(timeout, Type::Duration, source);
        Expect(";");
        AddInstruction({InstructionKind::Await, source, timeout, 0, 0, await});
    }
    void Statement() {
        const auto token = Take();
        if (token.Text == "let" || token.Text == "var") Declare(token.Text == "var", token.Source);
        else if (token.Text == "if" || token.Text == "while") Conditional(token.Source, token.Text == "while");
        else if (token.Text == "await") Await(token.Source);
        else if (token.Text == "stop") {
            Expect(";");
            AddInstruction({InstructionKind::Stop, token.Source});
        } else if (token.Text == "emit" || token.Text == "wait") {
            const auto expression = ParseExpression();
            RequireType(expression, token.Text == "emit" ? Type::Effect : Type::Duration, token.Source);
            Expect(";");
            AddInstruction({token.Text == "emit" ? InstructionKind::Emit : InstructionKind::Wait, token.Source, expression});
        } else {
            const auto binding = Lookup(token);
            if (!binding.Mutable) Fail(token.Source, "cannot assign an immutable binding");
            Expect("=");
            const auto expression = ParseExpression();
            RequireType(expression, binding.Kind, token.Source);
            Expect(";");
            AddInstruction({InstructionKind::Assign, token.Source, expression, 0, binding.Slot});
        }
    }
    std::size_t ParseExpression(int minimum = 0) {
        if (++ExpressionDepth > 24) Fail(Peek().Source, "expression nesting exceeds 24");
        auto left = Primary();
        while (true) {
            const auto token = Peek();
            const int priority = token.Text == "==" ? 1 : token.Text == "<" ? 2 : token.Text == "+" ? 3 : 0;
            if (priority == 0 || priority <= minimum) break;
            Take();
            const auto right = ParseExpression(priority);
            const auto type = Data->Expressions[left].Result;
            RequireType(right, type, token.Source);
            if (token.Text != "==" && !Numeric(type)) Fail(token.Source, "operator requires numbers of the same type");
            if (token.Text == "==" && type != Type::Bool && type != Type::Outcome && !Numeric(type))
                Fail(token.Source, "equality supports only bool, outcome and numbers");
            Expression expression;
            expression.Kind = token.Text == "+" ? ExpressionKind::Add : token.Text == "<" ? ExpressionKind::Less : ExpressionKind::Equal;
            expression.Result = token.Text == "+" ? type : Type::Bool;
            expression.Source = token.Source;
            expression.Arguments = {left, right};
            expression.Depth = 1 + std::max(Data->Expressions[left].Depth, Data->Expressions[right].Depth);
            left = AddExpression(expression);
        }
        --ExpressionDepth;
        return left;
    }
    std::size_t Call(Token name) {
        const auto found = std::find_if(Builtins.begin(), Builtins.end(), [&](auto &b) { return b.Name == name.Text; });
        if (found == Builtins.end()) Fail(name.Source, "unknown function; user functions and recursion are unsupported");
        Expression expression;
        expression.Kind = found->Kind;
        expression.Result = found->Result;
        expression.Source = name.Source;
        for (std::size_t i = 0; i < found->Count; ++i) {
            if (i) Expect(",");
            const auto arg = ParseExpression();
            RequireType(arg, found->Arguments[i], name.Source, true);
            expression.Arguments[i] = arg;
            expression.Depth = std::max(expression.Depth, Data->Expressions[arg].Depth + 1);
        }
        Expect(")");
        RequireCapability(found->Required, name.Source);
        return AddExpression(expression);
    }
    std::size_t Primary() {
        const auto token = Take();
        if (token.Text.empty()) Fail(token.Source, "expected an expression");
        if (token.Text == "(") {
            const auto value = ParseExpression();
            Expect(")");
            return value;
        }
        Expression expression;
        expression.Source = token.Source;
        if (token.Text == "!" || token.Text == "-") {
            const auto argument = ParseExpression(3);
            const auto type = Data->Expressions[argument].Result;
            if (token.Text == "!" ? type != Type::Bool : !Numeric(type)) Fail(token.Source, "invalid unary operand");
            expression.Kind = token.Text == "!" ? ExpressionKind::Not : ExpressionKind::Negate;
            expression.Result = type;
            expression.Arguments[0] = argument;
            expression.Depth = Data->Expressions[argument].Depth + 1;
        } else if (Digit(token.Text.front())) {
            if (token.Text.find('.') == std::string_view::npos) {
                std::int64_t value;
                const auto parsed = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != token.Text.data() + token.Text.size())
                    Fail(token.Source, "integer literal is out of range");
                expression.Result = Type::Integer;
                expression.Literal = value;
            } else {
                double value;
                const auto parsed = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), value);
                if (parsed.ec != std::errc{} || parsed.ptr != token.Text.data() + token.Text.size() || !std::isfinite(value))
                    Fail(token.Source, "number literal is out of range");
                expression.Result = Type::Number;
                expression.Literal = value;
            }
        } else if (token.Text == "true" || token.Text == "false") {
            expression.Literal = token.Text == "true";
        } else if (Accept("(")) return Call(token);
        else {
            constexpr std::array names{"applied", "rejected", "hit", "expired", "cancelled", "wake", "timeout"};
            const auto outcome = std::find(names.begin(), names.end(), token.Text);
            if (outcome != names.end()) {
                expression.Result = Type::Outcome;
                expression.Literal = static_cast<Outcome>(1 + std::distance(names.begin(), outcome));
            } else {
                const auto &binding = Lookup(token);
                expression.Kind = ExpressionKind::Local;
                expression.Result = binding.Kind;
                expression.Slot = binding.Slot;
            }
        }
        return AddExpression(expression);
    }

    std::shared_ptr<ArtifactData> Data;
    Capabilities Available;
    std::vector<Token> Tokens;
    std::vector<Binding> Bindings;
    std::size_t Cursor = 0, NextSlot = 0, ExpressionDepth = 0, BlockDepth = 0;
};

CompileResult Compile(std::string_view resource, std::string_view source, Capabilities available) {
    try {
        return Compiler(resource, source, available).Run();
    } catch (const CompileFailure &failure) {
        return Diagnostic{std::string(resource.substr(0, 256)), failure.Source, failure.Message};
    }
}
} // namespace pixel::formula::process
