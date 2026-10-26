#pragma once
#include "Formula/Process/Program.h"

namespace pixel {
// Natural crystal and player experiments use precisely the same artifact.
inline constexpr auto ForceGardenSource = R"(formula 2;
fn push(radius: cells, duration: ticks) -> action =
    field(radius, acceleration(3600), acceleration(0), duration);
run push(cells(8), ticks(600));
)";
inline const formula::process::ProgramArtifact &ForceGardenProgram() {
    static const auto program = std::get<formula::process::ProgramArtifact>(
        formula::process::Compile("home/force-crystal/2", ForceGardenSource));
    return program;
}
} // namespace pixel
