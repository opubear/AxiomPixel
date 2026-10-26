#pragma once
#include "Formula/Process/Program.h"

namespace pixel {
// A finite example shared by the player, natural source and fixed fixture.
inline formula::process::ProgramArtifact CarrierExample() {
    static const auto program = std::get<formula::process::ProgramArtifact>(formula::process::Compile(
        "examples/three-carriers", R"(formula 2;
fn impact_shot(velocity: speed, impact: action) -> action =
    on_hit(bolt(10, velocity, ticks(90)), impact);
fn burst(shot: action) -> action = repeat(3, ticks(30), shot);
let mining = dig(cells(2));
run burst(impact_shot(speed(60), mining));
)"));
    return program;
}
} // namespace pixel
