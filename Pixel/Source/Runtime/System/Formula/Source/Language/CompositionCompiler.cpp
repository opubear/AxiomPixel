#include "CompositionCompiler.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <map>
#include <memory>
#include <stdexcept>

namespace pixel::formula::process {
namespace {
enum class Kind { Number, Cells, Speed, Acceleration, Ticks, Action };
struct Failure { SourceSpan Source; std::string Message; };
struct Token { std::string_view Text; SourceSpan Source; };
bool Letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool Digit(char c) { return c >= '0' && c <= '9'; }
[[noreturn]] void Fail(SourceSpan source, std::string message) { throw Failure{source, std::move(message)}; }

std::vector<Token> Lex(std::string_view source) {
    std::vector<Token> result;
    std::size_t at = 0, line = 1, column = 1;
    const auto advance = [&] { if (source[at++] == '\n') { ++line; column = 1; } else ++column; };
    while (at < source.size()) {
        const char c = source[at];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (source.substr(at, 2) == "//") {
            while (at < source.size() && source[at] != '\n') advance();
            continue;
        }
        SourceSpan span{at, 1, line, column};
        if (Letter(c)) {
            do { advance(); } while (at < source.size() && (Letter(source[at]) || Digit(source[at])));
            if (at - span.Offset > 64) Fail(span, "identifier exceeds 64 bytes");
        } else if (Digit(c)) {
            do { advance(); } while (at < source.size() && Digit(source[at]));
            if (at < source.size() && source[at] == '.') {
                advance();
                while (at < source.size() && Digit(source[at])) advance();
            }
        } else if (std::string_view("();,:=-").find(c) != std::string_view::npos) {
            advance();
            if (c == '-' && at < source.size() && source[at] == '>') advance();
        } else Fail(span, "unexpected character in formula expression");
        span.Length = at - span.Offset;
        result.push_back({source.substr(span.Offset, span.Length), span});
    }
    result.push_back({{}, {source.size(), 0, line, column}});
    return result;
}

struct Signature {
    std::string_view Name;
    Kind Result;
    std::vector<Kind> Parameters;
    bool Variadic = false;
};
const std::vector<Signature> Builtins{
    {"cells", Kind::Cells, {Kind::Number}},
    {"speed", Kind::Speed, {Kind::Number}},
    {"acceleration", Kind::Acceleration, {Kind::Number}},
    {"ticks", Kind::Ticks, {Kind::Number}},
    {"add", Kind::Number, {Kind::Number, Kind::Number}},
    {"multiply", Kind::Number, {Kind::Number, Kind::Number}},
    {"dig", Kind::Action, {Kind::Cells}},
    {"wind", Kind::Action, {Kind::Cells, Kind::Speed, Kind::Speed}},
    {"bolt", Kind::Action, {Kind::Number, Kind::Speed, Kind::Ticks}},
    {"field", Kind::Action, {Kind::Cells, Kind::Acceleration, Kind::Acceleration, Kind::Ticks}},
    {"sequence", Kind::Action, {Kind::Action, Kind::Action}, true},
    {"delay", Kind::Action, {Kind::Ticks, Kind::Action}},
    {"repeat", Kind::Action, {Kind::Number, Kind::Ticks, Kind::Action}},
    {"on_hit", Kind::Action, {Kind::Action, Kind::Action}},
    {"if_credits", Kind::Action, {Kind::Number, Kind::Action, Kind::Action}},
    {"on_wake", Kind::Action, {Kind::Ticks, Kind::Action}},
    {"nothing", Kind::Action, {}}
};
bool Reserved(std::string_view name) {
    return name == "formula" || name == "fn" || name == "let" || name == "run"
        || std::any_of(Builtins.begin(), Builtins.end(), [&](const auto &b) { return b.Name == name; });
}
Kind ParseType(Token token) {
    constexpr std::array names{"number", "cells", "speed", "acceleration", "ticks", "action"};
    const auto it = std::find(names.begin(), names.end(), token.Text);
    if (it == names.end()) Fail(token.Source, "unknown type; expected number/cells/speed/acceleration/ticks/action");
    return static_cast<Kind>(it - names.begin());
}
struct Expression;
using Expr = std::shared_ptr<const Expression>;
struct Function {
    Signature Interface;
    Expr Body;
};
struct Expression {
    enum class Form { Literal, Parameter, Builtin, Call } Syntax;
    Kind Type;
    SourceSpan Source;
    double Number = 0;
    std::size_t Parameter = 0;
    std::string_view Name;
    std::shared_ptr<const Function> Definition;
    std::vector<Expr> Arguments;
};

class Parser {
  public:
    explicit Parser(std::string_view source) : Tokens(Lex(source)) {}
    Expr Parse() {
        Expect("formula"); Expect("2"); Expect(";");
        while (Peek().Text == "fn" || Peek().Text == "let") {
            const auto declaration = Take();
            const auto name = Name();
            if (Functions.contains(name.Text) || Values.contains(name.Text)) Fail(name.Source, "duplicate definition");
            if (Functions.size() + Values.size() == 64) Fail(name.Source, "definition limit exceeds 64");
            if (declaration.Text == "let") {
                Expect("="); Values.emplace(name.Text, ParseExpression()); Expect(";");
            } else Define(name);
        }
        Expect("run");
        auto root = ParseExpression();
        if (root->Type != Kind::Action) Fail(root->Source, "run requires an action description");
        Expect(";"); Expect("");
        return root;
    }

  private:
    const Token &Peek() const { return Tokens[Cursor]; }
    Token Take() { return Tokens[Cursor++]; }
    bool Accept(std::string_view text) { if (Peek().Text != text) return false; ++Cursor; return true; }
    Token Expect(std::string_view text) {
        if (Peek().Text != text) Fail(Peek().Source, "expected '" + std::string(text) + "'");
        return Take();
    }
    Token Name() {
        const auto token = Take();
        if (token.Text.empty() || !Letter(token.Text[0]) || Reserved(token.Text)) Fail(token.Source, "expected an unreserved name");
        return token;
    }
    void Define(Token name) {
        auto function = std::make_shared<Function>();
        function->Interface.Name = name.Text;
        Expect("(");
        if (Peek().Text != ")") do {
            const auto parameter = Name();
            if (Parameters.contains(parameter.Text) || Values.contains(parameter.Text) || Functions.contains(parameter.Text))
                Fail(parameter.Source, "parameter shadows an existing name");
            if (Parameters.size() == 8) Fail(parameter.Source, "function exceeds 8 parameters");
            Expect(":");
            const auto type = ParseType(Take());
            auto value = std::make_shared<Expression>();
            value->Syntax = Expression::Form::Parameter; value->Type = type; value->Source = parameter.Source;
            value->Parameter = Parameters.size();
            Parameters.emplace(parameter.Text, value);
            function->Interface.Parameters.push_back(type);
        } while (Accept(","));
        Expect(")"); Expect("->");
        function->Interface.Result = ParseType(Take());
        Expect("=");
        function->Body = ParseExpression();
        if (function->Body->Type != function->Interface.Result) Fail(function->Body->Source, "function return type mismatch");
        Expect(";");
        Parameters.clear();
        // Publish only after checking the body: recursive/forward calls cannot resolve.
        Functions.emplace(name.Text, std::move(function));
    }
    Expr ParseExpression() {
        if (++Depth > 24 || ++Nodes > 1024) Fail(Peek().Source, "expression depth/storage limit exceeded");
        const auto token = Take();
        auto value = std::make_shared<Expression>();
        value->Source = token.Source;
        bool negative = token.Text == "-";
        const auto literal = negative ? Take() : token;
        if (!literal.Text.empty() && Digit(literal.Text[0])) {
            const auto parsed = std::from_chars(literal.Text.data(), literal.Text.data() + literal.Text.size(), value->Number);
            if (parsed.ec != std::errc{} || parsed.ptr != literal.Text.data() + literal.Text.size()
                || !std::isfinite(value->Number) || value->Number > 1e9) Fail(token.Source, "number exceeds finite 1e9 limit");
            if (negative) value->Number = -value->Number;
            value->Syntax = Expression::Form::Literal; value->Type = Kind::Number;
        } else if (negative) Fail(token.Source, "minus requires a number literal");
        else if (Accept("(")) {
            const Signature *signature = nullptr;
            if (const auto it = Functions.find(token.Text); it != Functions.end()) {
                value->Syntax = Expression::Form::Call; value->Definition = it->second;
                signature = &value->Definition->Interface;
            } else {
                const auto builtin = std::find_if(Builtins.begin(), Builtins.end(), [&](const auto &b) { return b.Name == token.Text; });
                if (builtin == Builtins.end()) Fail(token.Source, "unknown function (definitions must precede calls; recursion is unsupported)");
                value->Syntax = Expression::Form::Builtin; signature = &*builtin;
            }
            value->Type = signature->Result; value->Name = signature->Name;
            if (Peek().Text != ")") do {
                if (value->Arguments.size() == 8) Fail(token.Source, "call exceeds 8 arguments");
                value->Arguments.push_back(ParseExpression());
            } while (Accept(","));
            Expect(")");
            const auto count = value->Arguments.size();
            if (signature->Variadic ? count < signature->Parameters.size() : count != signature->Parameters.size())
                Fail(token.Source, "function argument count mismatch");
            for (std::size_t i = 0; i < count; ++i)
                if (value->Arguments[i]->Type != signature->Parameters[std::min(i, signature->Parameters.size() - 1)])
                    Fail(value->Arguments[i]->Source, "function argument type mismatch");
        } else {
            --Depth;
            if (const auto it = Parameters.find(token.Text); it != Parameters.end()) return it->second;
            if (const auto it = Values.find(token.Text); it != Values.end()) return it->second;
            Fail(token.Source, "unknown immutable value");
        }
        --Depth;
        return value;
    }
    std::vector<Token> Tokens;
    std::size_t Cursor = 0, Depth = 0, Nodes = 0;
    std::map<std::string_view, Expr> Parameters, Values;
    std::map<std::string_view, std::shared_ptr<const Function>> Functions;
};

struct Description;
struct Value {
    double Number = 0;
    std::shared_ptr<const Description> Action;
};
struct Description {
    std::string_view Name;
    SourceSpan Source;
    std::vector<Value> Arguments;
};
class Builder {
  public:
    Value Evaluate(const Expr &expression, const std::vector<Value> &parameters = {}, std::size_t depth = 0) {
        if (++Work > 4096 || depth > 48) Fail(expression->Source, "description construction budget exceeded");
        if (expression->Syntax == Expression::Form::Literal) return {expression->Number, {}};
        if (expression->Syntax == Expression::Form::Parameter) return parameters.at(expression->Parameter);
        std::vector<Value> arguments;
        for (const auto &argument : expression->Arguments) arguments.push_back(Evaluate(argument, parameters, depth + 1));
        if (expression->Syntax == Expression::Form::Call) return Evaluate(expression->Definition->Body, arguments, depth + 1);
        const auto name = expression->Name;
        const auto number = [&](std::size_t i) { return arguments[i].Number; };
        if (expression->Type != Kind::Action) {
            double result = number(0);
            if (name == "add") result += number(1);
            if (name == "multiply") result *= number(1);
            if (!std::isfinite(result) || std::abs(result) > 1e9) Fail(expression->Source, "computed number exceeds finite 1e9 limit");
            if (name == "cells" && result < 0) Fail(expression->Source, "cells must be nonnegative");
            if (name == "ticks" && (result < 1 || result > 1000000 || std::floor(result) != result))
                Fail(expression->Source, "ticks must be an integer in 1..1000000");
            return {result, {}};
        }
        if (name == "repeat" && (number(0) < 1 || number(0) > 64 || std::floor(number(0)) != number(0)))
            Fail(expression->Source, "repeat count must be an integer in 1..64");
        if ((name == "if_credits" || name == "bolt") && number(0) < 0)
            Fail(expression->Source, "credit threshold/damage must be nonnegative");
        if (name == "if_credits" && std::floor(number(0)) != number(0)) Fail(expression->Source, "credit threshold must be an integer");
        if (name == "on_hit" && arguments[0].Action->Name != "bolt") Fail(expression->Source, "on_hit requires a bolt description");
        return {0, std::make_shared<const Description>(Description{name, expression->Source, std::move(arguments)})};
    }
  private:
    std::size_t Work = 0;
};

std::string Number(double value) {
    // The process lexer deliberately has no exponent syntax. Fixed conversion
    // preserves the exact double and the construction bound keeps text small.
    std::array<char, 384> bytes{};
    const auto result = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value, std::chars_format::fixed);
    if (result.ec != std::errc{}) throw std::logic_error("bounded number formatting failed");
    return {bytes.data(), result.ptr};
}
class Lowerer {
  public:
    CompositionSource Run(const Description &description) {
        Add("process 1;\n", description.Source);
        Action(description, 0);
        return std::move(Result);
    }
  private:
    void Add(std::string text, SourceSpan source) {
        const auto begin = Result.Process.size();
        if (begin + text.size() > 65536) Fail(source, "expanded description exceeds 65536 bytes");
        Result.Process += text;
        Result.Locations.push_back({begin, Result.Process.size(), source});
    }
    void Effect(const Description &action) {
        const auto n = [&](std::size_t i) { return Number(action.Arguments[i].Number); };
        std::string text = "emit " + std::string(action.Name) + "(";
        if (action.Name == "dig") text += "cells(" + n(0) + ")";
        if (action.Name == "wind") text += "cells(" + n(0) + "), speed(" + n(1) + "), speed(" + n(2) + ")";
        if (action.Name == "bolt") text += n(0) + ", speed(" + n(1) + "), ticks(" + n(2) + ")";
        if (action.Name == "field") text += "cells(" + n(0) + "), acceleration(" + n(1) + "), acceleration(" + n(2) + "), ticks(" + n(3) + ")";
        Add(text + ");\n", action.Source);
    }
    void Projectile(const Description &bolt, const Description *impact, std::size_t depth) {
        Add("scope_begin;\n", bolt.Source);
        Effect(bolt);
        Add("await hit within ticks(" + Number(bolt.Arguments[2].Number + 1) + ");\n", bolt.Source);
        Add("if outcome() == hit {\nscope_hit;\n", bolt.Source);
        if (impact) Action(*impact, depth + 1);
        Add("} else { if outcome() == expired {} else { stop; } }\nscope_end;\n", bolt.Source);
    }
    void Action(const Description &action, std::size_t depth) {
        if (++Visits > 1024 || depth > 20) Fail(action.Source, "expanded action depth/storage limit exceeded");
        const auto name = action.Name;
        const auto child = [&](std::size_t i) { Action(*action.Arguments[i].Action, depth + 1); };
        const auto n = [&](std::size_t i) { return Number(action.Arguments[i].Number); };
        if (name == "sequence") {
            for (const auto &argument : action.Arguments) Action(*argument.Action, depth + 1);
        } else if (name == "delay") {
            Add("wait ticks(" + n(0) + ");\n", action.Source); child(1);
        } else if (name == "repeat") {
            const auto counter = "iteration" + std::to_string(Counters++);
            Add("var " + counter + " = 0;\nwhile " + counter + " < " + n(0) + " {\n", action.Source);
            child(2);
            Add(counter + " = " + counter + " + 1;\nif " + counter + " < " + n(0) + " { wait ticks(" + n(1) + "); }\n}\n", action.Source);
        } else if (name == "if_credits") {
            Add("if inventory() < " + n(0) + " {\n", action.Source); child(2);
            Add("} else {\n", action.Source); child(1); Add("}\n", action.Source);
        } else if (name == "on_wake") {
            Add("await wake within ticks(" + n(0) + ");\nif outcome() == wake {\n", action.Source);
            child(1); Add("}\n", action.Source);
        } else if (name == "on_hit") Projectile(*action.Arguments[0].Action, action.Arguments[1].Action.get(), depth);
        else if (name == "bolt") Projectile(action, nullptr, depth);
        else if (name != "nothing") {
            Effect(action);
            Add("await result within ticks(1);\nif outcome() == applied {} else { stop; }\n", action.Source);
            // Admission consumes the first active boundary of the lifetime.
            if (name == "field" && action.Arguments[3].Number > 1)
                Add("wait ticks(" + Number(action.Arguments[3].Number - 1) + ");\n", action.Source);
        }
    }
    CompositionSource Result;
    std::size_t Counters = 0, Visits = 0;
};
} // namespace

SourceSpan CompositionSource::Original(SourceSpan generated) const {
    for (const auto &mapping : Locations)
        if (generated.Offset >= mapping.Begin && generated.Offset < mapping.End) return mapping.Source;
    return Locations.empty() ? SourceSpan{} : Locations.back().Source;
}
bool IsComposition(std::string_view source) {
    // Recognize the header without lexing the body twice, including leading comments.
    while (true) {
        const auto at = source.find_first_not_of(" \t\r\n");
        if (at == std::string_view::npos) return false;
        source.remove_prefix(at);
        if (!source.starts_with("//")) break;
        const auto end = source.find('\n');
        if (end == std::string_view::npos) return false;
        source.remove_prefix(end + 1);
    }
    return source.starts_with("formula") && (source.size() == 7 || (!Letter(source[7]) && !Digit(source[7])));
}
std::variant<CompositionSource, Diagnostic> LowerComposition(std::string_view resource, std::string_view source) {
    try {
        if (source.size() > 16384) Fail({}, "source exceeds 16384 bytes");
        auto root = Parser(source).Parse();
        const auto value = Builder().Evaluate(root);
        return Lowerer().Run(*value.Action);
    } catch (const Failure &failure) {
        return Diagnostic{std::string(resource.substr(0, 256)), failure.Source, failure.Message};
    }
}
} // namespace pixel::formula::process
