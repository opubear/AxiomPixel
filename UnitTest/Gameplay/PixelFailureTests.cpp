#include "../Support/TestAccess.h"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace pixel;
static_assert(std::is_same_v<decltype(std::declval<GameSession &>().GetWorld()), const World &>);
static_assert(std::is_same_v<decltype(std::declval<GameSession &>().GetFormulaRuntime()), const FormulaRuntime &>);
template <class T> concept MutableMapExposed = requires(T &world) { world.GetMutableMap(); };
static_assert(!MutableMapExposed<World>);

int main(int argc, char **argv) {
    // The wrapper requires this exact termination code AND the expected diagnostic.
    // Turning an internal exception into false/Rejected makes the test fail.
    std::set_terminate([] {
        try { std::rethrow_exception(std::current_exception()); }
        catch (const std::exception &error) { std::cerr << error.what() << std::endl; }
        std::_Exit(86);
    });
    if (argc != 2) return 2;
    const std::string_view mode(argv[1]);
    if (mode == "invalid-anchor") {
        World world(64, 64);
        FormulaRuntime runtime;
        runtime.Cast(world, {{}, {std::numeric_limits<double>::quiet_NaN(), 0}}, 0);
    } else if (mode == "invalid-artifact") {
        GameSession session;
        auto moved = formula::Formula::Bolt();
        const auto owned = std::move(moved);
        session.ApplyProgram(NativeCaller::Driver, moved);
    } else if (mode == "pending-step") {
        World world(64, 64);
        FormulaRuntime runtime;
        world.RestorePendingEnvironmentStep(0, true);
        runtime.Step(world);
    } else if (mode == "invalid-session-state") {
        GameSession session;
        testing::SessionAccess::NextGeneration(session).Kind = WorldGeneratorKind(999);
        session.Control(NativeCaller::Driver, api::ResetWorld{});
    } else return 2;
    return 0;
}
