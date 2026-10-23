#include "PixelDebugUI.h"
#include "Camera/GameCamera.h"
#include "Gameplay/GameSession.h"
#include "Performance/GameProfiler.h"
#include "PixelRender/GamePresentation.h"
#include "imgui.h"
#include <algorithm>
#include <stdexcept>

namespace pixel {
namespace {
void DrawCommit(const api::GameplayObservation &gameplay) {
    ImGui::TextDisabled("Frame: epoch %llu / tick %llu / revision %llu",
        static_cast<unsigned long long>(gameplay.Stamp.WorldEpoch),
        static_cast<unsigned long long>(gameplay.Stamp.Tick),
        static_cast<unsigned long long>(gameplay.Stamp.Revision));
}

const char *ProcessPhaseName(api::ProcessPhase phase) {
    switch (phase) {
    case api::ProcessPhase::Ready: return "ready";
    case api::ProcessPhase::Sleeping: return "waiting";
    case api::ProcessPhase::Awaiting: return "awaiting condition";
    case api::ProcessPhase::Ended: return "ended";
    }
    throw std::logic_error("unknown process presentation phase");
}

void DrawProcess(const api::ProcessObservation &process) {
    ImGui::Text("Program #%llu v%llu: %s (%s)",
        static_cast<unsigned long long>(process.Program.ResourceId),
        static_cast<unsigned long long>(process.Program.Version), ProcessPhaseName(process.Phase),
        process.Suspended ? "suspended" : process.Phase == api::ProcessPhase::Ended ? "inactive" : "active");
    ImGui::Text("Instance #%llu; active %llu ticks",
        static_cast<unsigned long long>(process.InstanceId),
        static_cast<unsigned long long>(process.ActiveSteps));
    ImGui::Text("Wait: %.*s; remaining %llu ticks", int(process.WaitingFor.size()), process.WaitingFor.data(),
        static_cast<unsigned long long>(process.RemainingSteps));
}

void DrawFieldSource(const api::FieldSourceObservation &source) {
    ImGui::Text("Field #%llu: %s; radius %.1f cells", static_cast<unsigned long long>(source.Handle),
        source.Suspended ? "suspended" : source.Contributing ? "active" : "inactive", source.RadiusCells);
    ImGui::Text("Acceleration: (%.1f, %.1f) cells/s^2; remaining %llu ticks",
        source.AccelerationCellsPerSecondSquared[0], source.AccelerationCellsPerSecondSquared[1],
        static_cast<unsigned long long>(source.RemainingSteps));
    ImGui::Text("Owner #%llu; instance #%llu; device #%llu",
        static_cast<unsigned long long>(source.OwnerId), static_cast<unsigned long long>(source.OwnerInstance),
        static_cast<unsigned long long>(source.DeviceId));
}

void DrawInventory(const GamePresentation &presentation) {
    if (!ImGui::CollapsingHeader("Inventory"))
        return;
    const auto &published = presentation.PublishedGameplay();
    if (!published) {
        ImGui::TextDisabled("Waiting for a committed frame.");
        return;
    }
    DrawCommit(*published);
    for (const auto &inventory : published->Inventories) {
        if (inventory.DeviceId)
            ImGui::Text("Device #%llu fuel: %u / %u", static_cast<unsigned long long>(inventory.DeviceId),
                        inventory.Quantity, inventory.Capacity);
        else
            ImGui::Text("Player inventory: %u / %u", inventory.Quantity, inventory.Capacity);
        ImGui::TextDisabled("Container #%llu v%llu; owner #%llu",
            static_cast<unsigned long long>(inventory.ContainerId), static_cast<unsigned long long>(inventory.Version),
            static_cast<unsigned long long>(inventory.OwnerActor));
        if (inventory.Stacks.empty())
            ImGui::TextDisabled("Empty");
        for (const auto &stack : inventory.Stacks) {
            std::string name(items::Definition(items::ItemId(stack.ItemId)).Name);
            std::replace(name.begin(), name.end(), '_', ' ');
            ImGui::BulletText("%s: %u (%u reserved)", name.c_str(), stack.Quantity, stack.Reserved);
        }
    }
}

void DrawProgramsAndFields(const GamePresentation &presentation) {
    if (!ImGui::CollapsingHeader("Programs and fields"))
        return;
    const auto &published = presentation.PublishedGameplay();
    if (!published) {
        ImGui::TextDisabled("Waiting for a committed frame.");
        return;
    }
    DrawCommit(*published);
    ImGui::Text("Programs: %zu; field sources: %zu", published->Processes.size(), published->FieldSources.size());
    for (const auto &process : published->Processes)
        DrawProcess(process);
    for (const auto &source : published->FieldSources)
        DrawFieldSource(source);
}

void DrawDevices(GameSession &session, GamePresentation &presentation,
                 std::array<int64_t, 2> &cell, uint64_t &selected) {
    if (!ImGui::CollapsingHeader("Devices"))
        return;
    ImGui::Checkbox("Place devices with left click (B)", &presentation.DevicePlacementMode);
    ImGui::TextUnformatted("Force emitter: 2 x 2 cells, right-side output port");
    ImGui::TextUnformatted("Build: 4 stone + 2 copper ore; 1 coal / 30 active ticks");
    ImGui::TextWrapped("F1: local force direction | Right click: enable/disable; Shift+right: recycle");
    const auto &published = presentation.PublishedGameplay();
    if (!published) {
        ImGui::TextDisabled("Waiting for a committed frame.");
        return;
    }
    const auto &gameplay = *published;
    DrawCommit(gameplay);
    const auto findSelected = [&] {
        return std::find_if(gameplay.Devices.begin(), gameplay.Devices.end(),
                            [&](const auto &device) { return device.Ref.Id == selected; });
    };
    if (findSelected() == gameplay.Devices.end())
        selected = gameplay.Devices.empty() ? 0 : gameplay.Devices.front().Ref.Id;

    ImGui::PushID("Devices");
    ImGui::InputScalar("Cell X", ImGuiDataType_S64, &cell[0]);
    ImGui::InputScalar("Cell Y", ImGuiDataType_S64, &cell[1]);
    if (ImGui::Button("Place force emitter")) {
        const auto receipt = session.Execute(NativeCaller::Debug, api::PlaceDevice{{cell[0], cell[1]}});
        if (receipt.Applied())
            if (const auto *placed = std::get_if<api::DeviceChanged>(&receipt.Result))
                selected = placed->Device.Ref.Id;
    }

    const auto label = selected ? "#" + std::to_string(selected) : "No devices";
    if (ImGui::BeginCombo("Device", label.c_str())) {
        for (const auto &device : gameplay.Devices) {
            const auto item = "#" + std::to_string(device.Ref.Id) + " (" + std::to_string(device.Anchor.X) + ", " + std::to_string(device.Anchor.Y) + ")";
            if (ImGui::Selectable(item.c_str(), selected == device.Ref.Id))
                selected = device.Ref.Id;
        }
        ImGui::EndCombo();
    }
    const auto device = findSelected();
    if (device != gameplay.Devices.end()) {
        ImGui::Text("State: %s", DeviceStateName(device->State));
        const auto reason = api::DeviceStateReason(device->State);
        ImGui::TextWrapped("Reason: %.*s", int(reason.size()), reason.data());
        const auto fuel = std::find_if(gameplay.Inventories.begin(), gameplay.Inventories.end(),
            [&](const auto &inventory) { return inventory.DeviceId == device->Ref.Id; });
        if (fuel != gameplay.Inventories.end())
            ImGui::Text("Fuel: %u / %u coal", fuel->Quantity, fuel->Capacity);
        else if (device->Fuel)
            ImGui::Text("Fuel: %u coal", *device->Fuel);
        else
            ImGui::TextDisabled("Fuel: not observable");
        ImGui::Text("Cycles: %llu; owner #%llu", static_cast<unsigned long long>(device->Cycles),
                    static_cast<unsigned long long>(device->OwnerActor));
        if (ImGui::Button("Supply 1 coal"))
            session.Execute(NativeCaller::Debug, api::SupplyDevice{device->Ref, 1});
        ImGui::SameLine();
        if (ImGui::Button(device->Enabled ? "Disable" : "Enable"))
            session.Execute(NativeCaller::Debug, api::SetDeviceEnabled{device->Ref, !device->Enabled});
        ImGui::SameLine();
        if (ImGui::Button("Recycle"))
            session.Execute(NativeCaller::Debug, api::RemoveDevice{device->Ref});
        const auto process = std::find_if(gameplay.Processes.begin(), gameplay.Processes.end(),
            [&](const auto &value) { return value.InstanceId == device->InstanceId; });
        if (process != gameplay.Processes.end()) DrawProcess(*process);
        else ImGui::TextDisabled("Program: no running instance");
        bool hasSource = false;
        for (const auto &source : gameplay.FieldSources)
            if (source.DeviceId == device->Ref.Id) {
                DrawFieldSource(source);
                hasSource = true;
            }
        if (!hasSource) ImGui::TextDisabled("Field: no source");
    }
    for (const auto caller : {NativeCaller::Input, NativeCaller::Debug})
        if (const auto *receipt = session.LastAction(caller))
            ImGui::TextWrapped("%s #%llu: %s %s", caller == NativeCaller::Input ? "Input" : "Debug",
                static_cast<unsigned long long>(receipt->Key.Sequence), receipt->Applied() ? "Applied" : "Rejected",
                receipt->Message.c_str());
    ImGui::PopID();
}

void DrawDevicePlacement(const GameSession &session, const GamePresentation &presentation) {
    if (!presentation.DevicePlacementMode)
        return;
    ImGui::SetNextWindowSize({std::max(200.f, std::min(520.f, ImGui::GetIO().DisplaySize.x - 32)), 0}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({16, ImGui::GetIO().DisplaySize.y - 16}, ImGuiCond_Always, {0, 1});
    if (ImGui::Begin("Device placement", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted("DEVICE PLACEMENT (B to exit)");
        ImGui::TextWrapped("Left click: force emitter | Cost: 4 stone + 2 copper ore");
        ImGui::TextWrapped("Right click device: enable/disable | Shift+right: recycle");
        if (const auto *receipt = session.LastAction(NativeCaller::Input))
            ImGui::TextWrapped("%s: %s", receipt->Applied() ? "Applied" : "Rejected", receipt->Message.c_str());
    }
    ImGui::End();
}

void DrawActions(GameSession &session, std::array<int64_t, 2> &cell) {
    if (!ImGui::CollapsingHeader("Game actions"))
        return;
    ImGui::InputScalar("Cell X", ImGuiDataType_S64, &cell[0]);
    ImGui::InputScalar("Cell Y", ImGuiDataType_S64, &cell[1]);
    const api::Cell target{cell[0], cell[1]};
    if (ImGui::Button("Mine cell"))
        session.Execute(NativeCaller::Debug, api::MineRegion{target, {}});
    ImGui::SameLine();
    if (ImGui::Button("Place dirt"))
        session.Execute(NativeCaller::Debug, api::PlaceMaterial{target});
    if (ImGui::Button("Activate at cell centre"))
        session.Execute(NativeCaller::Debug, api::ActivateProgram{session.AppliedProgram(), {target, {.5, .5}}});
    for (const auto caller : {NativeCaller::Input, NativeCaller::Debug}) {
        if (const auto *receipt = session.LastAction(caller))
            ImGui::TextWrapped("%s #%llu: %s %s", caller == NativeCaller::Input ? "Input" : "Debug",
                static_cast<unsigned long long>(receipt->Key.Sequence), receipt->Applied() ? "Applied" : "Rejected",
                receipt->Message.c_str());
    }
}

void DrawGeneration(GameSession &session) {
    if (!ImGui::CollapsingHeader("World generation", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    const auto &current = session.GetGenerationSettings();
    ImGui::Text("Current: %s; seed %llu", GeneratorName(current.Kind), static_cast<unsigned long long>(current.Seed));
    int selected = int(session.GetNextGenerationSettings().Kind);
    if (ImGui::Combo("Next world", &selected, "Superflat (debug)\0Natural\0"))
        session.Control(NativeCaller::Debug, api::SetNextGenerator{api::GeneratorKind(selected)});
    auto seed = session.GetNextGenerationSettings().Seed;
    if (ImGui::InputScalar("World seed", ImGuiDataType_U64, &seed))
        session.Control(NativeCaller::Debug, api::SetNextSeed{seed});
    ImGui::TextDisabled("Press R to save and create the selected world.");
    if (!session.IsReady()) {
        ImGui::TextWrapped("World recovery failed. Simulation and saving are disabled.");
        ImGui::TextWrapped("%s", session.GetSaveError().c_str());
        if (ImGui::Button("Retry world recovery"))
            session.Control(NativeCaller::Debug, api::RecoverWorld{});
    }
}

void DrawEnvironment(GameSession &session, GamePresentation &presentation) {
    const auto &World = session.GetWorld();
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(!session.IsReady());
        ImGui::BeginDisabled(!session.CanEditWorld());
        int64_t boundary = World.GetWindBoundary();
        ImGui::TextUnformatted("Wind blows above the world Y boundary.");
        ImGui::SetNextItemWidth(190);
        if (ImGui::InputScalar("Wind boundary Y", ImGuiDataType_S64, &boundary))
            session.Execute(NativeCaller::Debug, api::SetWindBoundary{boundary});
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Enter the world Y coordinate; wind covers cells above this boundary.");
        ImGui::Text("Wind coverage: %u rows in the player window", World.GetWindEndRow());
        ImGui::TextDisabled("Changing height restarts the wind simulation.");
        const auto &generation = session.GetGenerationSettings();
        const auto defaultBoundary = generation.SurfaceY - (generation.Kind == WorldGeneratorKind::Natural ? 152 : 88);
        const auto boundaryLabel = "Default boundary (Y = " + std::to_string(defaultBoundary) + ")";
        if (ImGui::Button(boundaryLabel.c_str()))
            session.Execute(NativeCaller::Debug, api::SetWindBoundary{defaultBoundary});
        int selected = int(session.GetSelectedWindStrategy());
        if (ImGui::Combo("Wind strategy", &selected, "Coarse NS\0Pixel reference (1:1)\0Procedural + obstacles\0Coarse NS + local model\0Adaptive NS (CPU prototype)\0LBM D2Q9 (CPU prototype)\0Empty (no wind)\0")) {
            session.Execute(NativeCaller::Debug, api::SelectWindStrategy{api::WindKind(selected)});
        }
        if (session.GetSelectedWindStrategy() == WindStrategyKind::Empty)
            ImGui::TextDisabled("No wind simulation; height is kept for comparison.");
        ImGui::EndDisabled();
        if (const auto *streamer = World.GetStreamer()) {
            if (World.GetEnvironment().IsWaitingForBudget())
                ImGui::TextWrapped("Simulation is waiting for world storage budget.");
            ImGui::Text("Resident: %zu chunks; loading/saving: %zu", World.GetMap().ChunkCount(), streamer->PendingCount());
            ImGui::Text("World storage: %.1f MiB", double(streamer->RetainedBytes()) / (1024 * 1024));
            if (!streamer->LastError().empty()) {
                ImGui::TextWrapped("World I/O: %s", streamer->LastError().c_str());
                if (ImGui::Button("Retry world I/O"))
                    session.Control(NativeCaller::Debug, api::RetryStreaming{});
            }
            if (ImGui::Button("Save world"))
                session.Control(NativeCaller::Debug, api::SaveWorld{});
        }
        bool paused = session.IsPaused();
        if (ImGui::Checkbox("Pause simulation", &paused))
            session.Control(NativeCaller::Debug, api::SetPaused{paused});
        if (paused && ImGui::Button("Step 1/60 second"))
            session.Control(NativeCaller::Debug, api::StepSimulation{});
        if (World.UsesPixelWind()) {
            const auto &report = World.GetPixelWind().GetReport();
            ImGui::Text("Divergence RMS: %.3g (%s)", report.RmsDivergence, report.Converged ? "converged" : "NOT converged");
            ImGui::TextDisabled("CPU reference: use single steps for large worlds.");
        }
        if (const auto *adaptive = World.GetAdaptiveWind()) {
            const auto &report = adaptive->GetReport();
            ImGui::Text("Leaves: %zu; face divergence RMS: %.3g (%s)", adaptive->GetLeafCount(), report.RmsDivergence, report.Converged ? "converged" : "NOT converged");
            ImGui::TextDisabled("First-order CPU prototype; use single steps.");
        }
        if (const auto *lbm = World.GetLbmWind()) {
            const auto &report = lbm->GetReport();
            ImGui::Text("Density: %.3g..%.3g; stabilizations: %llu", report.MinDensity, report.MaxDensity, static_cast<unsigned long long>(report.Stabilizations));
            ImGui::Text("Effective viscosity: %.3g", report.EffectiveViscosity);
            ImGui::TextDisabled("Weakly compressible CPU prototype; use single steps.");
        }
        bool fieldView = presentation.DisplayMode == GridDisplayMode::ForceField;
        if (ImGui::Checkbox("Field direction view (F1)", &fieldView))
            presentation.DisplayMode = fieldView ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
        ImGui::EndDisabled();
    }
}
void DrawWindAppearance(GamePresentation &presentation) {
    if (ImGui::CollapsingHeader("Wind appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        int mode = int(presentation.WindVisual.Mode);
        if (ImGui::Combo("Visualization", &mode, "Off\0Short trails\0Air refraction\0"))
            presentation.WindVisual.Mode = WindVisualMode(mode);
        ImGui::SliderFloat("Visibility", &presentation.WindVisual.Opacity, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Refraction", &presentation.WindVisual.RefractionPixels, 0.f, 12.f, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Flow motion", &presentation.WindVisual.MotionScale, 0.f, 2.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::TextDisabled("Appearance only; wind physics is unchanged.");
        ImGui::TextDisabled("Select a wind strategy to compare. F3: explore.");
    }
}
} // namespace
std::optional<ControlMode> PixelDebugUI::Draw(GameSession &session, GameCamera &camera,
                                               GamePresentation &presentation, ControlMode mode,
                                               performance::GameProfiler &performance) {
    PIXEL_PROFILE_SCOPE(DebugUI);
    std::optional<ControlMode> requestedMode;
    // A declined close/reset remains visible even when the debug panel is hidden.
    const auto &persistence = session.LastPersistenceResult();
    if (!persistence.Succeeded()) {
        ImGui::SetNextWindowSize({400, 0}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("World save", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
            ImGui::TextWrapped("%s", persistence.Message.c_str());
        ImGui::End();
    }
    if (Visible) {
        ImGui::SetNextWindowPos({16, 16}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({360, 0}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints({300, 120}, {std::max(300.f, ImGui::GetIO().DisplaySize.x - 32),
                                                       std::max(120.f, ImGui::GetIO().DisplaySize.y - 32)});
        if (ImGui::Begin("Pixel Debug", &Visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            DrawGeneration(session);
            DrawActions(session, ActionCell);
            DrawInventory(presentation);
            DrawDevices(session, presentation, DeviceCell, SelectedDevice);
            DrawProgramsAndFields(presentation);
            DrawEnvironment(session, presentation);
            DrawWindAppearance(presentation);
            ImGui::Checkbox("Formula editor (F4)", &FormulaEditor.Visible);
            bool debugCamera = mode == ControlMode::DebugCamera;
            if (ImGui::Checkbox("Free debug camera (F3)", &debugCamera))
                requestedMode = debugCamera ? ControlMode::DebugCamera : ControlMode::Standard;
            ImGui::TextUnformatted(debugCamera ? "DEBUG: WASD camera; +/- zoom; PageDown region" : "STANDARD: A/D move; Space jump; camera follows");
            float followRate = float(camera.GetCamera().GetFollowRate());
            if (ImGui::SliderFloat("Camera follow rate", &followRate, 0.5f, 20.f, "%.1f /s", ImGuiSliderFlags_AlwaysClamp))
                camera.GetCamera().SetFollowRate(followRate);
            ImGui::TextDisabled("F2: debug panel; F4: Formula Editor; T: tint");
            performance.DrawUI();
        }
        ImGui::End();
    }
    FormulaEditor.Draw(session);
    DrawDevicePlacement(session, presentation);
    return requestedMode;
}
} // namespace pixel
