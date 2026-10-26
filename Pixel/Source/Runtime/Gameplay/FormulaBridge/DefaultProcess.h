#pragma once

#include "Formula/Process/Program.h"

namespace pixel {
inline const formula::process::ProgramArtifact &DefaultPlayerProcess() {
    static const auto program = std::get<formula::process::ProgramArtifact>(
        formula::process::Compile("player/default-bolt", R"(formula 2;
run bolt(10, speed(60), ticks(120));
)"));
    return program;
}
} // namespace pixel
