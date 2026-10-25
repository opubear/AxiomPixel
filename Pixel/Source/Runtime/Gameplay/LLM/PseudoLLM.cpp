#include "PseudoLLM.h"
#include "DecisionAPI.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace pixel::llm {
namespace {
void Wait(const ModelInput &, DecisionAPI &api) {
    api.Explain("PseudoLLM randomly chose to wait.");
    api.Explain("No intervention is proposed for this prayer.");
    api.NoAction();
}

void OfferAid(const ModelInput &input, DecisionAPI &api) {
    api.Explain("PseudoLLM randomly chose to offer aid.");
    if (input.Intent.Device) {
        const auto device = std::find_if(input.Devices.begin(), input.Devices.end(),
            [&](const auto &value) { return value.Ref.Id == *input.Intent.Device; });
        if (device != input.Devices.end()) {
            api.Explain("Propose the approved program at the observed device.");
            api.ProposeAtDevice(device->Ref.Id);
            return;
        }
    } else {
        const auto cell = std::find_if(input.Materials.begin(), input.Materials.end(),
            [&](const auto &value) { return value.Location == input.Intent.Target && value.MaterialId.has_value(); });
        if (cell != input.Materials.end()) {
            api.Explain("Propose the approved program at the observed cell.");
            api.ProposeAtCell(cell->Location);
            return;
        }
    }
    api.Explain("The requested target is absent from the observation; propose no action.");
    api.NoAction();
}

// Add game decisions here as ordinary function compositions of the shared API.
// Selection is random; a chosen function only resolves its observed arguments.
using Decision = void (*)(const ModelInput &, DecisionAPI &);
constexpr std::array<Decision, 2> Decisions{Wait, OfferAid};
} // namespace

PseudoLLM::PseudoLLM() : PseudoLLM(std::random_device{}()) {}

PseudoLLM::PseudoLLM(uint32_t seed) : Random(seed) {}

bool PseudoLLM::TryStart(ModelInput input) {
    if (!input.Id || input.RetainedBytes() > ModelLimits::InputBytes
        || input.Materials.size() > ModelLimits::Cells || input.Devices.size() > ModelLimits::Devices)
        throw std::invalid_argument("pseudo LLM requires a bounded, identified input");
    if (Closed || Replies.size() == ModelLimits::InFlight) return false;
    if (std::any_of(Replies.begin(), Replies.end(), [&](const auto &reply) { return reply.Id == input.Id; }))
        throw std::logic_error("pseudo LLM received a duplicate active identity");

    DecisionAPI api(input.Id);
    const auto index = std::uniform_int_distribution<size_t>(0, Decisions.size() - 1)(Random);
    Decisions[index](input, api);
    Replies.push_back(std::move(api).Finish());
    return true;
}

std::optional<ModelReply> PseudoLLM::Poll() {
    if (Replies.empty()) return {};
    auto reply = std::move(Replies.front());
    Replies.pop_front();
    return reply;
}

void PseudoLLM::Cancel(DecisionId id) {
    std::erase_if(Replies, [&](const auto &reply) { return reply.Id == id; });
}

void PseudoLLM::Close() {
    Closed = true;
    Replies.clear();
}
} // namespace pixel::llm
