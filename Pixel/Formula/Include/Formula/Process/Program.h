#pragma once
#include "Formula/Process/HostContract.h"
#include <memory>
#include <string>
#include <string_view>
#include <variant>

namespace pixel::formula::process {
struct SourceSpan {
    std::size_t Offset = 0, Length = 0, Line = 1, Column = 1;
    bool operator==(const SourceSpan &) const = default;
};
struct Diagnostic {
    std::string Resource;
    SourceSpan Source;
    std::string Message;
};
struct ProgramIdentity {
    std::string Resource;
    std::string ContentSha256;
    std::uint32_t SemanticsVersion = 1;
    std::uint32_t HostContractVersion = 1;
    bool operator==(const ProgramIdentity &) const = default;
};
struct ArtifactData;
class Compiler;
class Instance;

// Published storage is immutable. Instances retain their exact artifact even
// when a resource is recompiled; no parsing or resource lookup occurs at Step.
class ProgramArtifact {
  public:
    const ProgramIdentity &Identity() const;
    Capabilities RequiredCapabilities() const;
    std::string_view Source() const;
    std::size_t InstructionCount() const;

  private:
    explicit ProgramArtifact(std::shared_ptr<const ArtifactData> data);
    std::shared_ptr<const ArtifactData> Data;
    friend class Compiler;
    friend class Instance;
};
using CompileResult = std::variant<ProgramArtifact, Diagnostic>;
CompileResult Compile(std::string_view resource, std::string_view source,
                      Capabilities available = AllCapabilities);
} // namespace pixel::formula::process
