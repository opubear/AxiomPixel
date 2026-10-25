#include "Formula/Formula.h"
#include <charconv>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <variant>

namespace pixel::formula {
namespace {
enum class TokenKind { Name,
                       Number,
                       Left,
                       Right,
                       Comma,
                       Equal,
                       Semicolon,
                       Pipe,
                       End };
struct Token {
    TokenKind Kind;
    std::string Text;
    SourceLocation Location;
};
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool NameStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

class Lexer {
  public:
    explicit Lexer(std::string_view source) : Source(source) {}
    Token Next() {
        SkipTrivia();
        const auto start = Offset;
        const auto location = Location;
        const char c = Peek();
        if (Offset == Source.size())
            return {TokenKind::End, "", location};
        if (NameStart(c)) {
            while (NameStart(Peek()) || Digit(Peek()))
                Take();
            return {TokenKind::Name, std::string(Source.substr(start, Offset - start)), location};
        }
        if (Digit(c) || c == '-' || c == '+' || c == '.') {
            if (c == '-' || c == '+')
                Take();
            bool digits = false;
            while (Digit(Peek())) {
                Take();
                digits = true;
            }
            if (Peek() == '.') {
                Take();
                while (Digit(Peek())) {
                    Take();
                    digits = true;
                }
            }
            if (!digits)
                throw ParseError(location, "expected a number");
            if (Peek() == 'e' || Peek() == 'E') {
                Take();
                if (Peek() == '+' || Peek() == '-')
                    Take();
                if (!Digit(Peek()))
                    throw ParseError(Location, "expected exponent digits");
                while (Digit(Peek()))
                    Take();
            }
            return {TokenKind::Number, std::string(Source.substr(start, Offset - start)), location};
        }
        Take();
        switch (c) {
        case '(':
            return {TokenKind::Left, "(", location};
        case ')':
            return {TokenKind::Right, ")", location};
        case ',':
            return {TokenKind::Comma, ",", location};
        case '=':
            return {TokenKind::Equal, "=", location};
        case ';':
            return {TokenKind::Semicolon, ";", location};
        case '|':
            if (Peek() == '>') {
                Take();
                return {TokenKind::Pipe, "|>", location};
            }
            break;
        }
        throw ParseError(location, "unexpected character '" + std::string(1, c) + "'");
    }

  private:
    std::string_view Source;
    std::size_t Offset = 0;
    SourceLocation Location;
    char Peek(std::size_t ahead = 0) const { return Offset + ahead < Source.size() ? Source[Offset + ahead] : '\0'; }
    void Take() {
        if (Source[Offset++] == '\n') {
            ++Location.Line;
            Location.Column = 1;
        } else
            ++Location.Column;
    }
    void SkipTrivia() {
        while (Offset < Source.size()) {
            if (Peek() == ' ' || Peek() == '\t' || Peek() == '\r' || Peek() == '\n')
                Take();
            else if (Peek() == '/' && Peek(1) == '/') {
                while (Offset < Source.size() && Peek() != '\n')
                    Take();
            } else
                break;
        }
    }
};

struct NumberArgument {
    double Value;
    Token Source;
};
using Argument = std::variant<NumberArgument, Formula>;
class Parser {
  public:
    explicit Parser(std::string_view source) : Tokens(source), Current(Tokens.Next()) {}
    Formula Program() {
        while (Current.Kind == TokenKind::Name && Current.Text == "let") {
            Advance();
            const auto name = Expect(TokenKind::Name, "binding name");
            if (name.Text == "let" || name.Text == "cast")
                throw ParseError(name.Location, "reserved binding name");
            if (Bindings.contains(name.Text))
                throw ParseError(name.Location, "duplicate binding '" + name.Text + "'");
            Expect(TokenKind::Equal, "'='");
            auto expression = Expression();
            Expect(TokenKind::Semicolon, "';'");
            Bindings.emplace(name.Text, std::move(expression));
        }
        if (Current.Kind != TokenKind::Name || Current.Text != "cast")
            throw ParseError(Current.Location, "expected 'let' or 'cast'");
        Advance();
        auto root = Expression();
        Expect(TokenKind::Semicolon, "';'");
        Expect(TokenKind::End, "end of program after one cast");
        return root;
    }

  private:
    Lexer Tokens;
    Token Current;
    std::unordered_map<std::string, Formula> Bindings;
    void Advance() { Current = Tokens.Next(); }
    Token Expect(TokenKind kind, const char *description) {
        if (Current.Kind != kind)
            throw ParseError(Current.Location, std::string("expected ") + description);
        auto token = Current;
        Advance();
        return token;
    }
    Formula Expression() {
        auto result = Primary();
        while (Current.Kind == TokenKind::Pipe) {
            Advance();
            const auto name = Expect(TokenKind::Name, "modifier after '|>'");
            if (name.Text != "damage_add" && name.Text != "damage_mul" && name.Text != "speed_mul"
                && name.Text != "delay" && name.Text != "repeat" && name.Text != "fan")
                throw ParseError(name.Location, "pipeline requires damage_add, damage_mul, speed_mul, delay, repeat or fan");
            result = Call(name, result);
        }
        return result;
    }
    Formula Primary() {
        if (Current.Kind == TokenKind::Left) {
            Advance();
            auto expression = Expression();
            Expect(TokenKind::Right, "')'");
            return expression;
        }
        const auto name = Expect(TokenKind::Name, "formula expression");
        if (Current.Kind == TokenKind::Left)
            return Call(name);
        const auto found = Bindings.find(name.Text);
        if (found == Bindings.end())
            throw ParseError(name.Location, "unknown binding '" + name.Text + "'");
        return found->second;
    }
    Formula Call(const Token &name, const std::optional<Formula> &input = std::nullopt) {
        Expect(TokenKind::Left, "'('");
        std::vector<Argument> args;
        if (Current.Kind != TokenKind::Right) {
            while (true) {
                if (Current.Kind == TokenKind::Number) {
                    auto token = Current;
                    Advance();
                    const char *begin = token.Text.data();
                    const char *end = begin + token.Text.size();
                    // from_chars is locale independent and preserves finite
                    // subnormals. Unlike the language, it rejects a leading '+'.
                    if (*begin == '+')
                        ++begin;
                    double value = 0;
                    const auto result = std::from_chars(begin, end, value);
                    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
                        throw ParseError(token.Location, "number must be finite and representable");
                    args.emplace_back(NumberArgument{value, token});
                } else
                    args.emplace_back(Expression());
                if (Current.Kind != TokenKind::Comma)
                    break;
                Advance();
            }
        }
        Expect(TokenKind::Right, "')'");
        if (input)
            args.emplace_back(*input);
        try {
            return Build(name, args);
        } catch (const std::invalid_argument &error) {
            throw ParseError(name.Location, error.what());
        }
    }
    static void Arity(const Token &name, const std::vector<Argument> &args, std::size_t count) {
        if (args.size() != count)
            throw ParseError(name.Location, name.Text + " expects " + std::to_string(count) + " arguments, got " + std::to_string(args.size()));
    }
    static const NumberArgument &Numeric(const Argument &arg, const Token &name) {
        if (auto value = std::get_if<NumberArgument>(&arg))
            return *value;
        throw ParseError(name.Location, name.Text + " expects a number here");
    }
    static const Formula &ExpressionArgument(const Argument &arg, const Token &name) {
        if (auto expression = std::get_if<Formula>(&arg))
            return *expression;
        throw ParseError(std::get<NumberArgument>(arg).Source.Location, name.Text + " expects a formula here");
    }
    static Tick Integer(const Argument &arg, const Token &name, Tick max = std::numeric_limits<Tick>::max()) {
        const auto &token = Numeric(arg, name).Source;
        Tick value = 0;
        const auto [end, error] = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), value);
        if (error != std::errc{} || end != token.Text.data() + token.Text.size() || value > max)
            throw ParseError(token.Location, "expected an unsigned decimal integer in range");
        return value;
    }
    static Formula Build(const Token &name, const std::vector<Argument> &args) {
        const auto &fn = name.Text;
        if (fn == "empty") {
            Arity(name, args, 0);
            return Formula{};
        }
        if (fn == "bolt") {
            if (args.empty())
                return Formula::Bolt();
            Arity(name, args, 3);
            return Formula::Bolt(Numeric(args[0], name).Value, Numeric(args[1], name).Value, Integer(args[2], name));
        }
        if (fn == "wind") {
            Arity(name, args, 2);
            return Formula::Wind(Numeric(args[0], name).Value, Numeric(args[1], name).Value);
        }
        if (fn == "dig") {
            Arity(name, args, 1);
            return Formula::Dig(Numeric(args[0], name).Value);
        }
        if (fn == "seq") {
            std::vector<Formula> children;
            for (const auto &arg : args)
                children.push_back(ExpressionArgument(arg, name));
            return Formula::Sequence(std::move(children));
        }
        if (fn == "on_hit") {
            Arity(name, args, 2);
            return ExpressionArgument(args[0], name).OnHit(ExpressionArgument(args[1], name));
        }
        if (fn == "damage_add" || fn == "damage_mul" || fn == "speed_mul" || fn == "delay") {
            Arity(name, args, 2);
            const auto &child = ExpressionArgument(args[1], name);
            if (fn == "delay")
                return child.After(Integer(args[0], name));
            const auto value = Numeric(args[0], name).Value;
            if (fn == "damage_add")
                return child.AddDamage(value);
            if (fn == "damage_mul")
                return child.MultiplyDamage(value);
            return child.MultiplySpeed(value);
        }
        if (fn == "repeat" || fn == "fan") {
            Arity(name, args, 3);
            const auto count = static_cast<std::uint32_t>(Integer(args[0], name, std::numeric_limits<std::uint32_t>::max()));
            const auto &child = ExpressionArgument(args[2], name);
            if (fn == "repeat")
                return child.Repeated(count, Integer(args[1], name));
            return child.Spread(count, Numeric(args[1], name).Value);
        }
        throw ParseError(name.Location, "unknown constructor '" + fn + "'");
    }
};
} // namespace
Formula Parse(std::string_view source) { return Parser(source).Program(); }
} // namespace pixel::formula
