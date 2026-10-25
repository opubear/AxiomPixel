#pragma once
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace pixel::serialization {
inline void Require(bool condition, const char *message) {
    if (!condition) throw std::invalid_argument(message);
}
// Length-prefixed strings and decimal scalars keep the format independent of
// native layout, locale and chunk edge. Doubles round trip all significant bits.
template<size_t MaximumBytes>
class Writer {
  public:
    Writer() {
        Out.imbue(std::locale::classic());
        Out << std::setprecision(std::numeric_limits<double>::max_digits10);
    }
    template<class... T> void Numbers(const T &...values) { ((Out << values << '\n'), ...); }
    template<class T> void Enum(T value) { Numbers(static_cast<unsigned>(value)); }
    void String(std::string_view value) { Numbers(value.size()); Out.write(value.data(), value.size()); Out << '\n'; }
    std::string Finish() const {
        auto value = Out.str();
        if (value.size() > MaximumBytes) throw std::logic_error("checkpoint exceeds format bound");
        return value;
    }
  private:
    std::ostringstream Out;
};

template<size_t MaximumBytes>
class Reader {
  public:
    explicit Reader(std::string_view source) : Input(source) {
        Require(source.size() <= MaximumBytes, "checkpoint exceeds format bound");
    }
    template<class T> T Number() {
        const auto end = Input.find('\n');
        Require(end != std::string_view::npos && end > 0 && end <= 128, "invalid checkpoint scalar");
        const auto token = Input.substr(0, end);
        T value{};
        const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
        Require(result.ec == std::errc{} && result.ptr == token.data() + token.size(), "invalid checkpoint number");
        if constexpr (std::is_floating_point_v<T>) Require(std::isfinite(value), "nonfinite checkpoint number");
        Input.remove_prefix(end + 1);
        return value;
    }
    template<class... T> void Numbers(T &...values) { ((values = Number<T>()), ...); }
    bool Boolean() { const auto value = Number<unsigned>(); Require(value <= 1, "invalid checkpoint boolean"); return value != 0; }
    template<class T> T Enum(T maximum) {
        const auto value = Number<unsigned>();
        Require(value <= static_cast<unsigned>(maximum), "invalid checkpoint enum");
        return static_cast<T>(value);
    }
    size_t Count(size_t maximum) { const auto size = Number<size_t>(); Require(size <= maximum, "checkpoint count exceeds bound"); return size; }
    std::string String(size_t maximum) {
        const auto size = Count(maximum);
        Require(size < Input.size() && Input[size] == '\n', "truncated checkpoint string");
        std::string result(Input.substr(0, size));
        Input.remove_prefix(size + 1);
        return result;
    }
    void End() const { Require(Input.empty(), "trailing checkpoint data"); }
  private:
    std::string_view Input;
};

} // namespace pixel::serialization
