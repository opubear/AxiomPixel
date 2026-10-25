#pragma once
#include "Formula/Process/Program.h"

namespace pixel {
// A finite example shared by the player, natural source and fixed fixture.
inline formula::process::ProgramArtifact CarrierExample() {
    static const auto program = std::get<formula::process::ProgramArtifact>(formula::process::Compile(
        "examples/three-carriers", R"(process 1;
var pulses = 0;
while pulses < 3 {
    emit bolt(10, speed(60), ticks(90));
    await hit within ticks(95);
    if outcome() == hit { emit dig(cells(2)); }
    pulses = pulses + 1;
    wait ticks(30);
}
)"));
    return program;
}
} // namespace pixel
