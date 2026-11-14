#include "PixelDebugUI.h"
#include "ForceGardenUI.h"
#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Dev/Debug/Performance/GameProfiler.h"
#include "Runtime/Render/GamePresentation.h"
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

void DrawDevices(GameSession &session, GamePresentation &presentation, GameInput &input,
                 std::array<int64_t, 2> &cell, uint64_t &selected, std::optional<api::RequestKey> &pendingPlacement) {
    if (!ImGui::CollapsingHeader("Devices"))
        return;
    bool placement = input.IsDevicePlacementEnabled();
    if (ImGui::Checkbox("Place devices with left click (B)", &placement))
        input.SetDevicePlacementEnabled(placement);
    ImGui::TextUnformatted("Force emitter: 2 x 2 cells, right-side output port");
    ImGui::TextUnformatted("Build: 4 stone + 2 copper ore; 1 coal / 30 active ticks");
    ImGui::TextWrapped("F1: local force direction | Right click: enable/disable; Shift+right: recycle");
    const auto &published = presentation.PublishedGameplay();
    if (!published) {
        ImGui::TextDisabled("Waiting for a committed frame.");
        return;
    }
    const auto &gameplay = *published;
    if (pendingPlacement) {
        const auto receipt = session.FindAction(*pendingPlacement);
        if (!receipt || !receipt->Queued()) {
            if (receipt && receipt->Applied())
                if (const auto *placed = std::get_if<api::DeviceChanged>(&receipt->Result))
                    selected = placed->Device.Ref.Id;
            pendingPlacement.reset();
        }
    }
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
        if (receipt.Queued()) pendingPlacement = receipt.Key;
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
                static_cast<unsigned long long>(receipt->Key.Sequence), receipt->Queued() ? "Queued" : receipt->Applied() ? "Applied" : "Rejected",
                receipt->Message.c_str());
    ImGui::PopID();
}

void DrawDevicePlacement(const GameSession &session, const GameInput &input) {
    if (!input.IsDevicePlacementEnabled())
        return;
    ImGui::SetNextWindowSize({std::max(200.f, std::min(520.f, ImGui::GetIO().DisplaySize.x - 32)), 0}, ImGuiCond_Always);
    ImGui::SetNextWindowPos({16, ImGui::GetIO().DisplaySize.y - 16}, ImGuiCond_Always, {0, 1});
    if (ImGui::Begin("Device placement", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::TextUnformatted("DEVICE PLACEMENT (B to exit)");
        ImGui::TextWrapped("Left click: force emitter | Cost: 4 stone + 2 copper ore");
        ImGui::TextWrapped("Right click device: enable/disable | Shift+right: recycle");
        if (const auto *receipt = session.LastAction(NativeCaller::Input))
            ImGui::TextWrapped("%s: %s", receipt->Queued() ? "Queued" : receipt->Applied() ? "Applied" : "Rejected", receipt->Message.c_str());
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
    if (ImGui::Button("Test workbench formula at cell centre"))
        session.Execute(NativeCaller::Debug, api::ActivateProgram{session.AppliedProgram(), {target, {.5, .5}}});
    for (const auto caller : {NativeCaller::Input, NativeCaller::Debug}) {
        if (const auto *receipt = session.LastAction(caller))
            ImGui::TextWrapped("%s #%llu: %s %s", caller == NativeCaller::Input ? "Input" : "Debug",
                static_cast<unsigned long long>(receipt->Key.Sequence), receipt->Queued() ? "Queued" : receipt->Applied() ? "Applied" : "Rejected",
                receipt->Message.c_str());
    }
}

void DrawGeneration(GameSession &session) {
    if (!ImGui::CollapsingHeader("World generation", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    const auto &current = session.GetGenerationSettings();
    ImGui::Text("Current: %s v%u; seed %llu", GeneratorName(current.Kind), current.Version, static_cast<unsigned long long>(current.Seed));
    int selected = int(session.GetNextGenerationSettings().Kind);
    if (ImGui::Combo("Next world", &selected, "Superflat (debug)\0Natural\0"))
        session.Control(NativeCaller::Debug, api::SetNextGenerator{api::GeneratorKind(selected)});
    auto seed = session.GetNextGenerationSettings().Seed;
    if (ImGui::InputScalar("World seed", ImGuiDataType_U64, &seed))
        session.Control(NativeCaller::Debug, api::SetNextSeed{seed});
    ImGui::TextDisabled(session.IsPersistent() ? "Press R to save and create the selected world."
                                             : "Press R to start a new world. Progress is not saved.");
    if (!session.IsReady()) {
        ImGui::TextWrapped("World recovery failed. Simulation and saving are disabled.");
        ImGui::TextWrapped("%s", session.GetSaveError().c_str());
        if (ImGui::Button("Retry world recovery"))
            session.Control(NativeCaller::Debug, api::RecoverWorld{});
    }
}

void DrawCalendar(GameSession &session) {
    if (!ImGui::CollapsingHeader("Day / night and seasons", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto calendar = session.GetCalendar().Snapshot();
    auto state = session.GetCalendar().State();
    ImGui::Text("Year %llu / %s %u / %02u:%02u (%s)", static_cast<unsigned long long>(calendar.Year),
        SeasonName(calendar.CurrentSeason), calendar.DayOfSeason, calendar.Hour, calendar.Minute, DayPhaseName(calendar.Phase));
    ImGui::Text("Sunrise %.1f h / sunset %.1f h", calendar.SunriseHour, calendar.SunsetHour);
    ImGui::BeginDisabled(!session.IsReady() || session.GetWorld().GetEnvironment().IsWaitingForBudget());
    bool changed = false;
    float hour = float(calendar.DayProgress * 24);
    if (ImGui::SliderFloat("Time of day", &hour, 0, 23.99f, "%.2f h", ImGuiSliderFlags_AlwaysClamp)) {
        state.TickOfDay = uint32_t(double(hour) / 24 * state.SecondsPerDay * WorldCalendar::TicksPerSecond);
        changed = true;
    }
    constexpr std::array phases{DayPhase::Dawn, DayPhase::Day, DayPhase::Dusk, DayPhase::Night};
    constexpr std::array labels{"Dawn", "Noon", "Dusk", "Midnight"};
    for (size_t index = 0; index < phases.size(); ++index) {
        if (index) ImGui::SameLine();
        if (ImGui::Button(labels[index])) {
            state.TickOfDay = session.GetCalendar().TickAtPhase(phases[index]);
            changed = true;
        }
    }
    int season = int(calendar.CurrentSeason);
    if (ImGui::Combo("Season", &season, "Spring\0Summer\0Autumn\0Winter\0")) {
        const auto yearStart = state.Day - state.Day % (state.DaysPerSeason * 4);
        const auto offset = uint64_t(season) * state.DaysPerSeason;
        if (offset < UINT64_MAX - yearStart) {
            state.Day = yearStart + offset; // preview the beginning of the selected season
            changed = true;
        }
    }
    int seconds = int(state.SecondsPerDay), days = int(state.DaysPerSeason), speed = int(state.TimeScale);
    if (ImGui::InputInt("Seconds per day", &seconds, 60, 600)) {
        state.SecondsPerDay = uint32_t(std::clamp(seconds, 1, 86400));
        state.TickOfDay = uint32_t(calendar.DayProgress * state.SecondsPerDay * WorldCalendar::TicksPerSecond);
        changed = true;
    }
    if (ImGui::SliderInt("Days per season", &days, 1, 365, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        state.DaysPerSeason = uint32_t(days);
        changed = true;
    }
    if (ImGui::SliderInt("Calendar speed", &speed, 1, 3600, "%dx", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
        state.TimeScale = uint32_t(speed);
        changed = true;
    }
    if (ImGui::Button("Reset calendar")) { state = {}; changed = true; }
    if (changed) {
        const auto result = session.Control(NativeCaller::Debug, api::SetWorldCalendar{state});
        if (!result.Succeeded()) ImGui::TextWrapped("%s", result.Message.c_str());
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Calendar speed changes light and seasons only. Game pause freezes time.");
}

void DrawEnvironment(GameSession &session, GamePresentation &presentation) {
    const auto &World = session.GetWorld();
    if (ImGui::CollapsingHeader("Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(!session.IsReady());
        ImGui::BeginDisabled(!session.CanEditWorld());
        const auto bounds = World.GetWindBounds();
        ImGui::TextUnformatted("Wind covers the entire player simulation window.");
        ImGui::Text("Wind coverage: %u x %u cells", bounds.Width, bounds.Height);
        int selected = int(session.GetSelectedWindStrategy());
        if (ImGui::Combo("Wind strategy", &selected, "Coarse NS\0Pixel reference (1:1)\0Procedural + obstacles\0Coarse NS + local model\0Adaptive NS (CPU prototype)\0LBM D2Q9 (CPU prototype)\0Empty (no wind)\0")) {
            session.Execute(NativeCaller::Debug, api::SelectWindStrategy{api::WindKind(selected)});
        }
        if (session.GetSelectedWindStrategy() == WindStrategyKind::Empty)
            ImGui::TextDisabled("No wind simulation.");
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
            if (session.IsPersistent() && ImGui::Button("Save world"))
                session.Control(NativeCaller::Debug, api::SaveWorld{});
        }
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
void PixelDebugUI::ApplyUIScale(float scale) {
    if (!ui::IsValidUIScale(scale))
        throw std::invalid_argument("invalid debug UI scale");
    if (!ImGui::GetCurrentContext() || AppliedUIScale == scale) return;
    if (!ScaleStyle) {
        ScaleStyle = [base = ImGui::GetStyle(), legacyFontScale = ImGui::GetIO().FontGlobalScale](float value) {
            auto &style = ImGui::GetStyle();
            style = base;
            style.ScaleAllSizes(value);
            style.FontScaleMain = base.FontScaleMain * legacyFontScale * value;
            ImGui::GetIO().FontGlobalScale = 1;
        };
    }
    // Rebuild from the initial style to avoid accumulating rounded dimensions.
    ScaleStyle(scale);
    AppliedUIScale = scale;
}

std::optional<ControlMode> PixelDebugUI::Draw(GameSession &session, GameCamera &camera,
                                               GamePresentation &presentation, GameInput &input,
                                               performance::GameProfiler &performance, will::WorldWill &worldWill, ui::InventoryUI &ui) {
    PIXEL_PROFILE_SCOPE(DebugUI);
    const auto mode = input.GetControlMode();
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
        const float scale = ui.GetUIScale();
        ImGui::SetNextWindowPos({16 * scale, 16 * scale}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({360 * scale, 0}, ImGuiCond_FirstUseEver);
        const ImVec2 available{std::max(1.f, ImGui::GetIO().DisplaySize.x - 32),
                               std::max(1.f, ImGui::GetIO().DisplaySize.y - 32)};
        ImGui::SetNextWindowSizeConstraints({std::min(300 * scale, available.x), std::min(120 * scale, available.y)}, available);
        if (ImGui::Begin("Pixel Debug", &Visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
            if (ImGui::CollapsingHeader("Display scale")) {
                float sceneScale = camera.GetSceneScale();
                if (ImGui::SliderFloat("SceneScale", &sceneScale, MinimumSceneScale, MaximumSceneScale, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
                    camera.SetSceneScale(sceneScale);
                float uiScale = ui.GetUIScale();
                if (ImGui::SliderFloat("UIScale", &uiScale, ui::MinimumUIScale, ui::MaximumUIScale, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
                    ui.SetUIScale(uiScale);
                if (ImGui::Button("Reset display scales")) {
                    camera.SetSceneScale(1);
                    ui.SetUIScale(1);
                }
                const auto size = camera.GetFramebufferSize();
                const float metersPerPixel = .5f / camera.GetView().CellSizePixels[0];
                ImGui::Text("Visible world: %.1f x %.1f m", size[0] * metersPerPixel, size[1] * metersPerPixel);
            }
            ImGui::BeginDisabled(!session.IsReady());
            bool paused = session.IsPaused();
            if (ImGui::Checkbox("Pause game", &paused))
                session.Control(NativeCaller::Debug, api::SetPaused{paused});
            if (paused) {
                ImGui::TextDisabled("World logic is frozen; F3 debug camera remains available.");
                if (ImGui::Button("Step game 1/60 second"))
                    session.Control(NativeCaller::Debug, api::StepSimulation{});
            }
            ImGui::EndDisabled();
            DrawGeneration(session);
            ImGui::BeginDisabled(!session.CanEditWorld());
            if (ImGui::Button("Drop wooden stick")) {
                const auto dropped = session.DropWoodenStick(NativeCaller::Debug);
                ItemStatus = dropped.Succeeded() ? "Wooden stick dropped to your right. Walk near it to pick it up." : dropped.Message;
            }
            ImGui::EndDisabled();
            if (!ItemStatus.empty()) ImGui::TextWrapped("%s", ItemStatus.c_str());
            if (ImGui::Button("Formula presets")) FormulaEditor.ShowPresets();
            DrawForceGardenUI(session, presentation, worldWill, GardenUI, ui.HeldItem());
            int brush = int(input.GetElementBrush());
            if (ImGui::Combo("Element brush (Shift+1-4)", &brush, "Normal tool\0Water\0Steam\0Fire\0"))
                input.SetElementBrush(ElementBrush(brush));
            if (brush != 0)
                ImGui::TextWrapped("Left click the world to spawn a radius-4 disk in empty cells. 1 restores the normal tool.");
            DrawActions(session, ActionCell);
            DrawInventory(presentation);
            DrawDevices(session, presentation, input, DeviceCell, SelectedDevice, PendingDevicePlacement);
            DrawProgramsAndFields(presentation);
            DrawCalendar(session);
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
    FormulaEditor.Draw(session, ui.HeldItem());
    DrawDevicePlacement(session, input);
    return requestedMode;
}
} // namespace pixel
