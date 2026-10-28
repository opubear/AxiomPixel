#include "FormulaDebugUI.h"
#include "Dev/Debug/Formula/FormulaPresets.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/Gameplay/FormulaBridge/FormulaBridge.h"
#include "Runtime/Gameplay/FormulaBridge/ProcessExample.h"
#include "imgui.h"
#include <algorithm>

namespace pixel {
void FormulaDebugUI::LoadSource(std::string_view source) {
    ProcessText.fill(0);
    std::copy(source.begin(), source.end(), ProcessText.begin());
    ProcessDraft.SetSource(std::string(source));
}

void FormulaDebugUI::DrawPresets(GameSession &session) {
    if (RevealPresets) ImGui::SetNextItemOpen(true);
    if (!ImGui::CollapsingHeader("Formula presets", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const auto presets = FormulaPresets();
    if (ImGui::BeginCombo("Preset", presets[SelectedPreset].Name)) {
        for (size_t i = 0; i < presets.size(); ++i) {
            if (ImGui::Selectable(presets[i].Name, SelectedPreset == i)) {
                SelectedPreset = i;
                PresetAngle = presets[i].AngleDegrees;
            }
        }
        ImGui::EndCombo();
    }
    const auto &preset = presets[SelectedPreset];
    ImGui::TextWrapped("%s", preset.Description);
    ImGui::TextWrapped("%s", preset.Hint);
    ImGui::TextDisabled("%s | offset (%.0f, %.0f) from player | 60 active ticks = 1 second",
        preset.Binding == ProcessBindingKind::Actor ? "Actor attachment" : "Fixed world origin",
        preset.Offset[0], preset.Offset[1]);
    ImGui::SliderFloat("Preset angle", &PresetAngle, -180, 180, "%.0f degrees", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::Button("Load preset source")) {
        LoadSource(preset.Source);
        ImGui::GetStateStorage()->SetInt(ImGui::GetID("Process source"), 1);
        Status = "Preset loaded into the editor. Compile and Apply after editing.";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!session.CanEditWorld() || !session.GetPlayer());
    if (ImGui::Button("Apply & run preset")) {
        const auto result = RunFormulaPreset(session, SelectedPreset, PresetAngle);
        Status = result.Succeeded() ? "Preset running. Right-click now uses this program too." : result.Message;
    }
    const auto instances = session.GetFormulaRuntime().InspectProcesses();
    const auto subject = FormulaPresetSubject(SelectedPreset);
    const auto matches = [subject](const auto &instance) {
        return instance.Carrier == ProcessCarrierKind::FixedFixture && instance.Subject == subject;
    };
    const auto count = std::count_if(instances.begin(), instances.end(), matches);
    const bool waiting = std::any_of(instances.begin(), instances.end(), [&](const auto &instance) {
        return matches(instance) && !instance.Suspended && instance.State.WakeSubscription.has_value();
    });
    if (preset.WaitsForWake) {
        ImGui::BeginDisabled(!waiting);
        if (ImGui::Button("Wake preset")) {
            const auto result = WakeFormulaPreset(session, SelectedPreset);
            Status = result.Succeeded() ? "Wake sent; the launcher fires on the next active tick." : result.Message;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
    }
    ImGui::BeginDisabled(count == 0);
    if (ImGui::Button("Stop preset")) {
        const auto result = StopFormulaPreset(session, SelectedPreset);
        Status = result.Succeeded() ? "Stopped this preset and its owned effects." : result.Message;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("Running: %d%s", int(count), waiting ? " | waiting for Wake" : "");
    if (!session.CanEditWorld()) ImGui::TextDisabled("Resume the game and finish world loading to run a preset.");
    ImGui::TextWrapped("Run restarts this preset at the player. Other presets keep running; Clear running stops all. Mining changes terrain.");
}

void FormulaDebugUI::DrawProcessEditor(GameSession &session) {
    if (!ImGui::CollapsingHeader("Process source", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::TextWrapped("formula 2: immutable functions and action composition. Legacy process 1 sources remain readable.");
    if (ImGui::Button("Load three-pulse draft")) LoadSource(CarrierExample().Source());
    ImGui::SameLine();
    const auto &active = session.GetFormulaRuntime().ActiveProcess();
    if (ImGui::Button("Load applied process")) LoadSource(active.Source());
    if (ImGui::InputTextMultiline("Process text", ProcessText.data(), ProcessText.size(), {-1, 160}))
        ProcessDraft.SetSource(ProcessText.data());
    if (ImGui::Button("Compile process")) ProcessDraft.Build();
    ImGui::SameLine();
    ImGui::BeginDisabled(!ProcessDraft.Program());
    if (ImGui::Button("Apply process")) {
        const auto result = session.ApplyProcess(NativeCaller::Debug, *ProcessDraft.Program());
        Status = result ? "Process published. Existing instances retain their version; right-click starts the new version." : result.Message;
    }
    ImGui::EndDisabled();
    if (const auto &error = ProcessDraft.Error())
        ImGui::TextWrapped("%s:%zu:%zu: %s", error->Resource.c_str(), error->Source.Line, error->Source.Column, error->Message.c_str());
    if (const auto &program = ProcessDraft.Program())
        ImGui::Text("Compiled %.12s | %zu instructions", program->Identity().ContentSha256.c_str(), program->InstructionCount());
    ImGui::Text("Applied v%llu | language %u | %.12s", static_cast<unsigned long long>(session.AppliedProgram().Version),
        active.Identity().SemanticsVersion, active.Identity().ContentSha256.c_str());
    ImGui::TextDisabled("Compile then Apply. Applied programs and running instances are saved; editor drafts are temporary.");
}

void FormulaDebugUI::DrawProcessExample(GameSession &session) {
    if (!ImGui::CollapsingHeader("Process: three carriers")) return;
    ImGui::TextWrapped("Each pulse launches a ball and digs on hit. Natural source, fixed fixture and right-click use the same program. Unpause or Step to advance.");
    const auto *player = session.GetPlayer();
    ImGui::BeginDisabled(!player || !session.CanEditWorld());
    for (const auto kind : {ProcessCarrierKind::NaturalSource, ProcessCarrierKind::FixedFixture}) {
        const bool natural = kind == ProcessCarrierKind::NaturalSource;
        if (ImGui::Button(natural ? "Start natural source" : "Start fixed fixture")) {
            ProcessCarrier carrier;
            carrier.Kind = kind;
            carrier.Subject = 1;
            carrier.Origin = FormulaWorldAdapter::Offset(player->GetWorldPosition(), {natural ? 8.0 : -8.0, 1});
            carrier.Scope = session.GetWorld().GetSimulationBounds();
            carrier.AngleDegrees = 45;
            carrier.Credits = natural ? 6 : 4;
            const auto result = session.StartProcessFixture(NativeCaller::Debug, carrier);
            Status = result.Succeeded() ? "Started fixture; Clear running removes all carriers." : result.Message;
        }
        if (natural) ImGui::SameLine();
    }
    if (ImGui::Button("Start actor attachment")) {
        ProcessCarrier carrier;
        carrier.Kind = ProcessCarrierKind::FixedFixture;
        carrier.Subject = 9001;
        carrier.Binding = ProcessBindingKind::Actor;
        carrier.AttachedActor = session.GetActors().PlayerSnapshot()->Id;
        carrier.LocalOffset = {3, 1};
        carrier.Scope = session.GetWorld().GetSimulationBounds();
        const auto result = session.StartProcessFixture(NativeCaller::Debug, carrier);
        Status = result.Succeeded() ? "Attached to player; future requests follow movement within the authorized scope." : result.Message;
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Fixture credits: natural 6, fixed 4, player 64; one credit per accepted effect.");
    const auto &bridge = session.GetFormulaRuntime();
    const auto stats = bridge.ProcessStats();
    ImGui::Text("Process instances %zu | events %zu | last boundary work %llu | accepted %llu / refused %llu",
        stats.Instances, stats.PendingEvents, static_cast<unsigned long long>(stats.Work),
        static_cast<unsigned long long>(stats.Accepted), static_cast<unsigned long long>(stats.Rejected));
    for (const auto &instance : bridge.InspectProcesses()) {
        const auto &state = instance.State;
        const char *phase = instance.Suspended ? "frozen"
            : state.Status == formula::process::Phase::Sleeping ? "wait" : "await";
        ImGui::Text("#%llu v%llu %.12s | %s PC %zu | active %llu remaining %llu | credits %lld",
            static_cast<unsigned long long>(state.Id), static_cast<unsigned long long>(instance.Version),
            instance.Program.ContentSha256.c_str(), phase, state.Instruction,
            static_cast<unsigned long long>(state.ActiveSteps), static_cast<unsigned long long>(state.Remaining),
            static_cast<long long>(instance.Credits));
        const auto &binding = instance.Configuration;
        const auto cell = binding.Origin.Cell();
        ImGui::Text("  %s actor %llu | source %u/%llu | cell (%lld,%lld) | angle %.1f",
            binding.Binding == ProcessBindingKind::Actor ? "attached" : "world",
            static_cast<unsigned long long>(binding.AttachedActor.Value), unsigned(binding.Kind),
            static_cast<unsigned long long>(binding.Subject), static_cast<long long>(cell.X),
            static_cast<long long>(cell.Y), binding.AngleDegrees);
    }
}

void FormulaDebugUI::DrawDiagnostics(GameSession &session) {
    const auto &runtime = session.GetFormulaRuntime();
    const auto stats = runtime.Stats();
    ImGui::Text("Instances %zu | balls %zu | casts %llu accepted, %llu refused", stats.Roots, stats.Projectiles,
        static_cast<unsigned long long>(stats.AcceptedThisFrame), static_cast<unsigned long long>(stats.RejectedThisFrame));
    if (!session.GetFormulaError().empty())
        ImGui::TextWrapped("Activation: %s", session.GetFormulaError().c_str());
    if (session.IsPaused()) ImGui::TextWrapped("Game paused. Step advances the whole world by one tick.");
    if (session.GetWorld().GetEnvironment().IsWaitingForBudget())
        ImGui::TextWrapped("Waiting for world storage budget: new casts and terrain edits are refused until retry succeeds.");
    ImGui::BeginChild("Formula events", {0, 120}, true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto &entry : runtime.Diagnostics())
        ImGui::Text("E%llu C%llu t%llu: %s", static_cast<unsigned long long>(entry.WorldEpoch),
            static_cast<unsigned long long>(entry.CastId), static_cast<unsigned long long>(entry.Tick), entry.Message.c_str());
    const auto &receipts = runtime.ProcessReceipts();
    const auto first = receipts.size() > 8 ? receipts.size() - 8 : 0;
    for (size_t i = first; i < receipts.size(); ++i) {
        const auto &receipt = receipts[i];
        ImGui::Text("#%llu request %llu | %s:%zu:%zu | result %u refusal %u",
            static_cast<unsigned long long>(receipt.Request.Instance), static_cast<unsigned long long>(receipt.Request.Sequence),
            receipt.Program.Resource.c_str(), receipt.Source.Line, receipt.Source.Column,
            unsigned(receipt.Outcome), unsigned(receipt.Refusal));
    }
    ImGui::EndChild();
}

void FormulaDebugUI::Draw(GameSession &session) {
    if (!Visible) return;
    if (!Initialized) {
        LoadSource(session.GetFormulaRuntime().ActiveProcess().Source());
        Initialized = true;
    }
    const auto display = ImGui::GetIO().DisplaySize;
    const ImVec2 initialSize{std::min(850.f, std::max(240.f, display.x - 32)),
                            std::min(720.f, std::max(240.f, display.y - 32))};
    ImGui::SetNextWindowPos({std::max(16.f, std::min(400.f, display.x - initialSize.x - 16)), 16}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
    if (RevealPresets) {
        ImGui::SetNextWindowCollapsed(false);
        ImGui::SetNextWindowFocus();
    }
    if (ImGui::Begin("Formula Editor", &Visible, ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Applied process v%llu", static_cast<unsigned long long>(session.AppliedProgram().Version));
        if (ImGui::Button("Clear running")) {
            const auto result = session.Control(NativeCaller::Debug, api::ClearRunning{});
            Status = result.Succeeded() ? "Cleared running instances." : result.Message;
        }
        if (RevealPresets) ImGui::SetScrollY(0);
        DrawPresets(session);
        if (!Status.empty()) ImGui::TextWrapped("%s", Status.c_str());
        DrawProcessEditor(session);
        DrawProcessExample(session);
        DrawDiagnostics(session);
    }
    ImGui::End();
    RevealPresets = false;
}
} // namespace pixel
