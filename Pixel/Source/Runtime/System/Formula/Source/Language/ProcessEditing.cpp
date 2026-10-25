#include "Formula/Process/Editing.h"

namespace pixel::formula::process::editing {
Draft::Draft(std::string resource) : Resource(std::move(resource)) {}

void Draft::SetSource(std::string source) {
    if (source == Text) return;
    Text = std::move(source);
    Compiled.reset();
    Failure.reset();
}

bool Draft::Build(Capabilities available) {
    Compiled.reset();
    Failure.reset();
    auto result = Compile(Resource, Text, available);
    if (const auto *error = std::get_if<Diagnostic>(&result)) Failure = *error;
    else Compiled = std::get<ProgramArtifact>(std::move(result));
    return Compiled.has_value();
}
} // namespace pixel::formula::process::editing
