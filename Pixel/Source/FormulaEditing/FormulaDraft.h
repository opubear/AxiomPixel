#pragma once
#include "FormulaValidation/FormulaAnalysis.h"
#include <memory>
#include <optional>

namespace pixel::formula::editing {
using NodeId = std::uint64_t;

struct DraftNode {
    NodeId Id;
    NodeKind Kind;
    NodeValues Values;
    std::vector<std::unique_ptr<DraftNode>> Children;
};

// IDs belong to this draft and are never reused, including after reset/revert.
class Draft {
  public:
    Draft();
    DraftNode &Root();
    const DraftNode &Root() const;
    DraftNode *Find(NodeId id);
    const DraftNode *Find(NodeId id) const;
    // Rejects descriptions above 64 nodes/depth 16 before recursive import.
    // An invalid_argument leaves the current tree and ID sequence unchanged.
    void FromFormula(const Formula &formula);

    // Operations return the node to select, or 0 when the operation is invalid.
    // Growth is checked before allocation: rejection preserves the whole tree.
    NodeId AddChild(NodeId parent, NodeKind kind);
    NodeId Replace(NodeId id, NodeKind kind);
    NodeId Wrap(NodeId id, NodeKind kind);
    NodeId Unwrap(NodeId id, std::size_t child = 0);
    NodeId Delete(NodeId id);
    // Copies to the next sequence slot. A root is wrapped in a sequence first.
    NodeId Duplicate(NodeId id);
    bool Move(NodeId id, int direction);

  private:
    NodeId NextId = 1;
    std::unique_ptr<DraftNode> Tree;
    std::unique_ptr<DraftNode> NewNode(NodeKind kind);
    std::unique_ptr<DraftNode> CopyNode(const DraftNode &node);
    std::unique_ptr<DraftNode> Import(const Formula &formula);
};

struct CompileError {
    NodeId Node = 0;
    std::string Field;
    std::string Message;
};

struct CompileResult {
    std::optional<Formula> Description;
    validation::Analysis Budget;
    std::vector<CompileError> Errors;
    explicit operator bool() const { return Description.has_value() && Errors.empty(); }
};

// Validates editor structure, then uses the shared game-host analysis rules.
CompileResult Compile(const Draft &draft);
} // namespace pixel::formula::editing
