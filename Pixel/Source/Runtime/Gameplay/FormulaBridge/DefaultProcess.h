#pragma once

#include "Formula/Process/Program.h"

namespace pixel {
inline const formula::process::ProgramArtifact &DefaultPlayerProcess() {
    static const auto program = std::get<formula::process::ProgramArtifact>(
        formula::process::Compile("player/default-bolt", R"(process 1;
emit bolt(10, speed(60), ticks(120));
await hit within ticks(121);
)"));
    return program;
}
} // namespace pixel
