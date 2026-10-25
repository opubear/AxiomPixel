#pragma once
#include "Formula/Process/Program.h"

namespace pixel {
// Natural crystal and player experiments use precisely the same artifact.
inline constexpr auto ForceGardenSource = R"(process 1;
emit field(cells(8), acceleration(3600), acceleration(0), ticks(601));
await result within ticks(1);
let source = effect_handle();
wait ticks(599);
emit stop_field(source);
)";
inline const formula::process::ProgramArtifact &ForceGardenProgram() {
    static const auto program = std::get<formula::process::ProgramArtifact>(
        formula::process::Compile("home/force-crystal/1", ForceGardenSource));
    return program;
}
} // namespace pixel
