#pragma once

#include "Formula/Formula.h"

namespace pixel::formula::validation {
// Current game-host policy, not limits of the standalone Formula language.
inline constexpr std::size_t MaximumNodes = 64;
inline constexpr std::size_t MaximumDepth = 16;
inline constexpr std::size_t MaximumEffects = 64;
inline constexpr std::size_t MaximumVisits = 4096;
inline constexpr Tick MaximumEndTick = 3600;

struct Analysis {
    std::size_t Nodes = 0;
    std::size_t Depth = 0;
    std::size_t Effects = 0;
    std::size_t WindEffects = 0;
    std::size_t NodeVisits = 0;
    Tick EndTick = 0;
};

// Child slots from the root identify an occurrence, including shared Formula
// values used in multiple branches. Editor identities stay in FormulaEditing.
struct Diagnostic {
    std::vector<std::size_t> Path;
    std::string Field;
    std::string Message;
};

struct AnalysisResult {
    Analysis Budget;
    std::vector<Diagnostic> Errors;
    explicit operator bool() const { return Errors.empty(); }
};

// Bounded shape check before recursive import or analysis; nullptr means valid.
const char *ShapeError(const Formula &formula);
// Parameter diagnostics have an empty Path; callers attach their own location.
std::vector<Diagnostic> ValidateParameters(NodeKind kind, const NodeValues &values);
AnalysisResult Analyze(const Formula &formula);
} // namespace pixel::formula::validation
