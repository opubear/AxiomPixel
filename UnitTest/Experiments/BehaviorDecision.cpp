#include "BehaviorDecision.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace pixel::experiment {
namespace {
constexpr std::string_view Aid = "Propose the approved program at the observed target.";
constexpr std::string_view Missing = "The requested target is not observed; propose no action.";

bool DeviceVisible(const llm::ModelInput &input) {
    return input.Intent.Device && std::any_of(input.Devices.begin(), input.Devices.end(),
        [&](const auto &device) { return device.Ref.Id == *input.Intent.Device; });
}

bool CellVisible(const llm::ModelInput &input) {
    return std::any_of(input.Materials.begin(), input.Materials.end(), [&](const auto &cell) {
        return cell.Location == input.Intent.Target && cell.MaterialId.has_value();
    });
}

void CheckInput(const llm::ModelInput &input) {
    if (!input.Id || input.Materials.size() > llm::ModelLimits::Cells
        || input.Devices.size() > llm::ModelLimits::Devices || input.RetainedBytes() > llm::ModelLimits::InputBytes)
        throw std::invalid_argument("decision experiment requires bounded host input");
}
} // namespace

llm::ModelReply NativeAidDecision(const llm::ModelInput &input) {
    CheckInput(input);
    llm::DecisionAPI api(input.Id);
    const bool visible = input.Intent.Device ? DeviceVisible(input) : CellVisible(input);
    api.Explain(visible ? Aid : Missing);
    if (!visible) api.NoAction();
    else if (input.Intent.Device) api.ProposeAtDevice(*input.Intent.Device);
    else api.ProposeAtCell(input.Intent.Target);
    return std::move(api).Finish();
}

std::variant<DecisionGraph, Diagnostic> DecisionGraph::Compile(uint32_t version, std::span<const Instruction> code) {
    if (version != 1) return Diagnostic{0, "unsupported decision graph version"};
    if (code.empty() || code.size() > MaxInstructions) return Diagnostic{0, "instruction capacity exceeded"};
    size_t textBytes = 0;
    for (size_t i = 0; i < code.size(); ++i) {
        const auto &op = code[i];
        auto forward = [&](size_t target) { return target > i && target < code.size(); };
        if (op.Code != Op::Explain && !op.Text.empty()) return Diagnostic{i, "unexpected text operand"};
        switch (op.Code) {
        case Op::DeviceGoal: case Op::DeviceVisible: case Op::CellVisible:
            if (!forward(op.Yes) || !forward(op.No)) return Diagnostic{i, "branches must point forward inside the graph"};
            break;
        case Op::Explain:
            if (!forward(op.Yes) || op.No) return Diagnostic{i, "explanation requires one forward successor"};
            if (op.Text.size() + 1 > llm::ModelLimits::ExplanationBytes - textBytes)
                return Diagnostic{i, "explanation capacity exceeded"};
            textBytes += op.Text.size() + 1; // Conservative sum over all paths, including separators.
            break;
        case Op::AtDevice: case Op::AtCell: case Op::NoAction:
            if (op.Yes || op.No) return Diagnostic{i, "terminal cannot continue or create a second proposal"};
            break;
        default: return Diagnostic{i, "unsupported operation"};
        }
    }
    return DecisionGraph(code);
}

llm::ModelReply DecisionGraph::Evaluate(const llm::ModelInput &input) const {
    CheckInput(input);
    llm::DecisionAPI api(input.Id);
    size_t pc = 0;
    for (size_t work = 0; work < MaxInstructions; ++work) {
        const auto &op = Code.at(pc);
        switch (op.Code) {
        case Op::DeviceGoal: pc = input.Intent.Device ? op.Yes : op.No; break;
        case Op::DeviceVisible: pc = DeviceVisible(input) ? op.Yes : op.No; break;
        case Op::CellVisible: pc = CellVisible(input) ? op.Yes : op.No; break;
        case Op::Explain: api.Explain(op.Text); pc = op.Yes; break;
        case Op::AtDevice:
            if (!input.Intent.Device || !*input.Intent.Device)
                return {input.Id, llm::ModelStatus::Invalid, {}, "device proposal requires a device goal"};
            api.ProposeAtDevice(*input.Intent.Device);
            return std::move(api).Finish();
        case Op::AtCell:
            api.ProposeAtCell(input.Intent.Target);
            return std::move(api).Finish();
        case Op::NoAction:
            api.NoAction();
            return std::move(api).Finish();
        default: throw std::logic_error("invalid compiled decision operation");
        }
    }
    throw std::logic_error("validated forward graph did not terminate");
}

size_t DecisionGraph::RetainedBytes() const {
    size_t bytes = sizeof(*this) + Code.capacity() * sizeof(Instruction);
    for (const auto &op : Code) bytes += op.Text.capacity() + 1;
    return bytes; // Conservative: includes small-string inline capacity twice.
}

DecisionGraph AidDecisionGraph() {
    const std::array<Instruction, 9> code{{
        {Op::DeviceGoal, 1, 2},
        {Op::DeviceVisible, 3, 7},
        {Op::CellVisible, 5, 7},
        {Op::Explain, 4, 0, std::string(Aid)},
        {Op::AtDevice},
        {Op::Explain, 6, 0, std::string(Aid)},
        {Op::AtCell},
        {Op::Explain, 8, 0, std::string(Missing)},
        {Op::NoAction}
    }};
    return std::get<DecisionGraph>(DecisionGraph::Compile(1, code));
}
} // namespace pixel::experiment
