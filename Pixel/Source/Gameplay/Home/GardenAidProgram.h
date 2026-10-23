#pragma once
#include "Formula/Process/Program.h"

namespace pixel {
inline const formula::process::ProgramArtifact &GardenAidProgram() {
    static const auto program = std::get<formula::process::ProgramArtifact>(
        formula::process::Compile("home/world-will-aid/1",
            "process 1; emit field(cells(16), acceleration(3600), acceleration(0), ticks(600)); "
            "await result within ticks(1); wait ticks(600);"));
    return program;
}
} // namespace pixel
