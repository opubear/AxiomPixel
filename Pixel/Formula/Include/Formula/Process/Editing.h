#pragma once
#include "Formula/Process/Program.h"

namespace pixel::formula::process::editing {
// Editing never mutates a published artifact. Changing text retires the old
// compile result so Apply cannot accidentally publish an obsolete draft.
class Draft {
  public:
    explicit Draft(std::string resource);
    void SetSource(std::string source);
    const std::string &Source() const { return Text; }
    bool Build(Capabilities available = AllCapabilities);
    const std::optional<ProgramArtifact> &Program() const { return Compiled; }
    const std::optional<Diagnostic> &Error() const { return Failure; }

  private:
    std::string Resource, Text;
    std::optional<ProgramArtifact> Compiled;
    std::optional<Diagnostic> Failure;
};
} // namespace pixel::formula::process::editing
