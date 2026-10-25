#pragma once
#include "Runtime/Gameplay/LLM/DecisionAPI.h"
#include <span>
#include <variant>

// M6 comparison only. No production target links this candidate representation.
namespace pixel::experiment {
enum class Op { DeviceGoal, DeviceVisible, CellVisible, Explain, AtDevice, AtCell, NoAction };
struct Instruction {
    Op Code;
    size_t Yes = 0, No = 0;
    std::string Text;
};
struct Diagnostic { size_t InstructionIndex; std::string Message; };

// A finite decision DAG, not an installed or persistent behavior process.
// No backward edges, waits, children, mutable locals or host calls are representable.
class DecisionGraph {
  public:
    static constexpr size_t MaxInstructions = 16;
    static std::variant<DecisionGraph, Diagnostic> Compile(uint32_t version, std::span<const Instruction> code);
    llm::ModelReply Evaluate(const llm::ModelInput &input) const;
    size_t RetainedBytes() const;

  private:
    explicit DecisionGraph(std::span<const Instruction> code) : Code(code.begin(), code.end()) {}
    std::vector<Instruction> Code;
};

llm::ModelReply NativeAidDecision(const llm::ModelInput &input);
DecisionGraph AidDecisionGraph();
} // namespace pixel::experiment
