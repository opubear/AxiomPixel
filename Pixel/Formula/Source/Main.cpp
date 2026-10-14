#include "Formula/Simulator.h"
#include <charconv>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>

using namespace pixel::formula;
namespace {
Tick ReadTick(std::string_view text) {
    Tick result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("ticks must be unsigned decimal integers");
    return result;
}
void Usage() {
    std::cout << "Usage: PixelFormulaDemo file.formula [--ticks N] [--hit-at N]\n"
                 "Runs an independent 60 Hz trace (default: 60 ticks).\n"
                 "--hit-at explicitly injects one hit for every live projectile at N; no collision world is loaded.\n";
}
} // namespace
int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        Usage();
        return 0;
    }
    if (argc < 2) {
        Usage();
        return 2;
    }
    try {
        Tick endTick = 60;
        std::optional<Tick> hitTick;
        for (int i = 2; i < argc; i += 2) {
            if (i + 1 == argc)
                throw std::invalid_argument("missing option value");
            const std::string_view option(argv[i]);
            if (option == "--ticks")
                endTick = ReadTick(argv[i + 1]);
            else if (option == "--hit-at")
                hitTick = ReadTick(argv[i + 1]);
            else
                throw std::invalid_argument("unknown option: " + std::string(option));
        }
        if (hitTick && *hitTick > endTick)
            throw std::invalid_argument("--hit-at must be <= --ticks");
        std::ifstream file(argv[1]);
        if (!file)
            throw std::invalid_argument("cannot open: " + std::string(argv[1]));
        const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto formula = Parse(source);
        std::cout << "Description: " << Format(formula) << '\n';
        Simulator simulator;
        simulator.Cast(formula);
        if (hitTick) {
            simulator.AdvanceTo(*hitTick);
            const auto carriers = simulator.Projectiles();
            std::cout << "Injecting demo hits at tick " << *hitTick << " for " << carriers.size() << " live projectile(s).\n";
            for (const auto &projectile : carriers)
                simulator.Hit(projectile.Id, projectile.Position);
        }
        simulator.AdvanceTo(endTick);
        for (const auto &entry : simulator.Trace())
            std::cout << FormatTrace(entry) << '\n';
        std::cout << "End tick=" << simulator.Now() << " active=" << simulator.Projectiles().size() << '\n';
        return 0;
    } catch (const ParseError &error) {
        std::cerr << argv[1] << ':' << error.what() << '\n';
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "Formula prototype: " << error.what() << '\n';
        return 1;
    }
}
