#include "ForceGardenUI.h"
#include "Gameplay/Home/ForceGardenProgram.h"
#include "PixelRender/GamePresentation.h"
#include "imgui.h"
#include <algorithm>
#include <stdexcept>

namespace pixel {
namespace {
const char *ConditionReason(GardenCondition condition) {
    switch (condition) {
    case GardenCondition::OutsideActiveArea: return "Frozen: outside the active island (no catch-up).";
    case GardenCondition::Obstructed: return "Blocked: the bed needs air and solid ground below.";
    case GardenCondition::NeedsForce: return "Needs rightward force: below +1800 cells/s^2.";
    case GardenCondition::ExcessForce: return "Too much force: above +5400 cells/s^2.";
    case GardenCondition::Growing: return "Growing: air, support and force are suitable.";
    case GardenCondition::Mature: return "Mature: accumulated 180 healthy steps.";
    }
    throw std::logic_error("unknown garden condition");
}

void DrawBed(const char *label, int64_t x, const ForceGardenObservation &garden,
             const GardenSample &sample, uint32_t growth) {
    ImGui::Text("%s (%lld, %lld): %u / %u steps", label, static_cast<long long>(x),
                static_cast<long long>(garden.State.SurfaceY - 1), growth, GardenMatureSteps);
    ImGui::ProgressBar(float(growth) / GardenMatureSteps, {-1, 0});
    if (sample.Sampled)
        ImGui::Text("Horizontal acceleration: %+.1f cells/s^2", sample.HorizontalAcceleration);
    else
        ImGui::TextDisabled("Horizontal acceleration: not sampled");
    ImGui::TextWrapped("%s", ConditionReason(sample.Condition));
}

void RecordAction(std::string &status, const char *operation, const api::ActionReceipt &receipt) {
    status = std::string(operation) + (receipt.Applied() ? ": Applied. " : ": Rejected. ") + receipt.Message;
}

void MineSupply(GameSession &session, int64_t first, int64_t last, int64_t y, std::string &status) {
    uint32_t collected = 0;
    for (int64_t x = first; x <= last; ++x) {
        const auto receipt = session.Execute(NativeCaller::Debug, api::MineRegion{{x, y}, {}});
        if (!receipt.Applied()) {
            status = "Mining stopped after " + std::to_string(collected) + " cells: " + receipt.Message;
            return;
        }
        collected += std::get<api::MaterialResult>(receipt.Result).Changed;
    }
    status = "Mining: Applied. Collected " + std::to_string(collected) + " resource cells into player inventory.";
}
} // namespace

void DrawForceGardenUI(GameSession &session, const GamePresentation &presentation, std::string &receipt) {
    if (!ImGui::CollapsingHeader("Force garden"))
        return;
    ImGui::TextWrapped("Observe the wild crystal, reproduce its force at the home bed, then sustain growth with a coal-powered emitter.");
    if (ImGui::Button("Save and create force garden")) {
        const auto result = session.Control(NativeCaller::Debug, api::CreateForceGarden{});
        receipt = std::string("Create garden: ") + (result.Succeeded() ? "Applied. " : "Rejected. ") + result.Message;
    }
    const auto &published = presentation.PublishedGameplay();
    if (published && published->Garden) {
        const auto &garden = *published->Garden;
        const auto y = garden.State.SurfaceY;
        ImGui::TextDisabled("Frame: epoch %llu / tick %llu / revision %llu",
            static_cast<unsigned long long>(published->Stamp.WorldEpoch),
            static_cast<unsigned long long>(published->Stamp.Tick),
            static_cast<unsigned long long>(published->Stamp.Revision));
        ImGui::Text("Crystal (%lld, %lld): %s", static_cast<long long>(GardenCrystalX),
            static_cast<long long>(y - 4), garden.State.CrystalEnabled ? "enabled" : "disabled by Clear running");
        ImGui::TextWrapped("1. Inspect the cyan crystal and wild bed. F1 shows force direction; F2 shows this panel. The crystal repeats every 600 steps without fuel.");
        DrawBed("Wild bed", GardenWildX, garden, garden.Wild, garden.State.WildGrowth);
        DrawBed("Home bed", GardenHomeX, garden, garden.Home, garden.State.HomeGrowth);
        ImGui::TextWrapped("Growth needs air, solid support below, and rightward acceleration +1800..+5400 cells/s^2 for 180 successful steps (3 active seconds). Progress is retained when conditions fail.");

        ImGui::Separator();
        ImGui::TextWrapped("2. Copy the crystal's exact program. Walk within 8 cells of the home bed (%lld, %lld), then right-click toward it. The temporary field starts in front of the player; the click only aims it.",
            static_cast<long long>(GardenHomeX), static_cast<long long>(y - 1));
        if (ImGui::Button("Apply crystal program")) {
            const auto result = session.ApplyProcess(NativeCaller::Debug, ForceGardenProgram());
            receipt = result ? "Apply crystal program: Applied. Stand near the home bed and right-click toward it; existing instances keep their program."
                             : "Apply crystal program: Rejected. " + result.Message;
        }
        if (ImGui::TreeNode("Crystal program source")) {
            ImGui::TextUnformatted(ForceGardenSource);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::TextWrapped("3. Mine the marked supply row at Y=%lld: X=516..519 stone, 520..521 copper ore, 522..537 coal. These buttons mine real cells.", static_cast<long long>(y + 2));
        if (ImGui::Button("Mine build materials")) MineSupply(session, 516, 521, y + 2, receipt);
        if (ImGui::Button("Mine 16 coal")) MineSupply(session, 522, 537, y + 2, receipt);
        ImGui::TextWrapped("4. Build at (%lld, %lld), supply 8 coal, then enable. Cost: 4 stone + 2 copper ore; 1 coal / 30 active steps. The outlet reaches the home bed.",
            static_cast<long long>(GardenDeviceX), static_cast<long long>(y - 2));
        if (ImGui::Button("Build home emitter"))
            RecordAction(receipt, "Build home emitter", session.Execute(NativeCaller::Debug, api::PlaceDevice{{GardenDeviceX, y - 2}}));
        const auto device = std::find_if(published->Devices.begin(), published->Devices.end(), [&](const auto &value) {
            return value.Anchor == api::Cell{GardenDeviceX, y - 2};
        });
        if (device != published->Devices.end()) {
            ImGui::Text("Home emitter: %s; fuel %u coal", DeviceStateName(device->State), device->Fuel.value_or(0));
            const auto reason = api::DeviceStateReason(device->State);
            ImGui::TextWrapped("%.*s", int(reason.size()), reason.data());
            if (ImGui::Button("Supply home 8 coal"))
                RecordAction(receipt, "Supply home 8 coal", session.Execute(NativeCaller::Debug, api::SupplyDevice{device->Ref, 8}));
            if (ImGui::Button(device->Enabled ? "Disable home emitter" : "Enable home emitter"))
                RecordAction(receipt, "Toggle home emitter", session.Execute(NativeCaller::Debug, api::SetDeviceEnabled{device->Ref, !device->Enabled}));
        }
        ImGui::TextWrapped("Leaving the active island freezes both beds and their sources. Clear running also disables the natural crystal; create a new garden to restart it.");
    } else {
        ImGui::TextDisabled("Create a garden to begin. The current world is saved first.");
    }
    if (!receipt.empty()) ImGui::TextWrapped("Last garden operation: %s", receipt.c_str());
}
} // namespace pixel
