#pragma once
#include "Formula/Process/Program.h"
#include <vector>

namespace pixel::formula::process {
// Internal lowering boundary. Generated text is never exported as user source.
struct CompositionSource {
    struct Mapping { std::size_t Begin, End; SourceSpan Source; };
    std::string Process;
    std::vector<Mapping> Locations;
    SourceSpan Original(SourceSpan generated) const;
};
bool IsComposition(std::string_view source);
std::variant<CompositionSource, Diagnostic> LowerComposition(std::string_view resource, std::string_view source);
} // namespace pixel::formula::process
