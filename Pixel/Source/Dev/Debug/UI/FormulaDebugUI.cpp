#include "FormulaDebugUI.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/Gameplay/FormulaBridge/FormulaBridge.h"
#include "Runtime/Gameplay/FormulaBridge/ProcessExample.h"
#include "imgui.h"
#include <algorithm>

namespace pixel {
namespace {
using formula::NodeKind;
using formula::editing::NodeId;
constexpr const char *KindOptions = "Empty\0Bolt (base input)\0Wind\0Dig\0Sequence\0Add damage\0Multiply damage\0Multiply speed\0Delay\0Repeat\0Fan\0On hit\0";

bool EditParameters(formula::editing::DraftNode &node) {
    auto &v = node.Values;
    bool changed = false;
    const auto number = [&](const char *label, double &value) {
        changed |= ImGui::InputDouble(label, &value, 0, 0, "%.6g");
    };
    const auto ticks = [&](const char *label, formula::Tick &value) {
        changed |= ImGui::InputScalar(label, ImGuiDataType_U64, &value);
    };
    const auto count = [&] { changed |= ImGui::InputScalar("Count (0..16)", ImGuiDataType_U32, &v.Count); };
    switch (node.Kind) {
    case NodeKind::Bolt:
        number("Damage metadata", v.Damage);
        number("Speed (cells/s)", v.Speed);
        ticks("Lifetime (1..600 ticks)", v.Lifetime);
        ImGui::TextDisabled("Yellow circle; radius 0.35 cells; no gravity or wind drift.");
        break;
    case NodeKind::Wind:
        number("Radius (cells)", v.Radius);
        number("Impulse (cells/s)", v.Impulse);
        ImGui::TextWrapped("World effect requires enabled Coarse wind with at most 1024 cells. Radius below 1/256 cell is skipped.");
        break;
    case NodeKind::Dig:
        number("Radius (cells)", v.Radius);
        break;
    case NodeKind::AddDamage: number("Add damage", v.Amount); break;
    case NodeKind::MultiplyDamage: number("Damage factor", v.Factor); break;
    case NodeKind::MultiplySpeed: number("Speed factor", v.Factor); break;
    case NodeKind::Delay: ticks("Delay (0..600 ticks)", v.Ticks); break;
    case NodeKind::Repeat:
        count();
        ticks("Interval (0..600 ticks)", v.Interval);
        break;
    case NodeKind::Fan:
        count();
        number("Spacing (degrees)", v.SpacingDegrees);
        break;
    case NodeKind::OnHit:
        ImGui::TextWrapped("Carrier flies now; Payload runs one successful world tick after impact. Carrier modifiers do not modify Payload.");
        break;
    case NodeKind::Sequence: ImGui::TextDisabled("Children execute in order."); break;
    case NodeKind::Empty: ImGui::TextDisabled("Produces no projectile or world effect."); break;
    }
    return changed;
}

bool CanWrap(NodeKind kind) {
    return kind == NodeKind::Sequence || kind >= NodeKind::AddDamage;
}
} // namespace

void FormulaDebugUI::Rebuild() {
    Compiled = formula::editing::Compile(Draft);
    Preview = Compiled ? "cast " + formula::Format(*Compiled.Description) + ";" : "Draft is invalid; fix the errors before applying.";
}

void FormulaDebugUI::LoadApplied(GameSession &session) {
    Draft.FromFormula(session.GetFormulaRuntime().ActiveFormula());
    Selected = Draft.Root().Id;
    Dirty = false;
    Status.clear();
    Rebuild();
}

void FormulaDebugUI::SelectOperation(NodeId selection) {
    if (!selection) {
        Status = "Operation unavailable for this slot, or it would exceed 64 nodes / 16 levels.";
        return;
    }
    Selected = selection;
    Dirty = true;
    Status.clear();
    Rebuild();
}

void FormulaDebugUI::DrawToolbar(GameSession &session) {
    const auto &runtime = session.GetFormulaRuntime();
    ImGui::Text("Applied version %llu | %s", static_cast<unsigned long long>(runtime.Version()),
        runtime.AppliedProgram().ResourceId == 2 ? "process active; tree Apply switches back" : Dirty ? "draft modified" : "draft matches applied");
    ImGui::BeginDisabled(!Compiled);
    if (ImGui::Button("Apply")) {
        const auto result = session.ApplyProgram(NativeCaller::Debug, *Compiled.Description);
        if (result) {
            Dirty = false;
            Status = "Applied. Existing instances keep their previous description.";
        } else {
            Status = result.Message;
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Revert to applied"))
        LoadApplied(session);
    ImGui::SameLine();
    if (ImGui::Button("Reset draft to bolt")) {
        Draft.FromFormula(formula::Formula::Bolt());
        SelectOperation(Draft.Root().Id);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear running")) {
        const auto result = session.Control(NativeCaller::Debug, api::ClearRunning{});
        Status = result.Succeeded() ? "Cleared running instances." : result.Message;
    }

    static constexpr const char *presets[]{"Single bolt", "Three-way fan", "Delayed triple", "On hit: dig", "On hit: wind"};
    if (ImGui::BeginCombo("Preset (draft only)", "Choose preset")) {
        for (size_t index = 0; index < std::size(presets); ++index) {
            if (!ImGui::Selectable(presets[index]))
                continue;
            auto description = formula::Formula::Bolt();
            if (index == 1) description = description.Spread(3, 12);
            if (index == 2) description = description.Repeated(3, 12).After(30);
            if (index == 3) description = description.OnHit(formula::Formula::Dig(3));
            if (index == 4) description = description.OnHit(formula::Formula::Wind(6, 120));
            Draft.FromFormula(description);
            SelectOperation(Draft.Root().Id);
        }
        ImGui::EndCombo();
    }
    if (!Status.empty())
        ImGui::TextWrapped("%s", Status.c_str());
}

void FormulaDebugUI::DrawProcessEditor(GameSession &session) {
    if (!ImGui::CollapsingHeader("Process source")) return;
    const auto load = [&](std::string_view source) {
        ProcessText.fill(0);
        std::copy(source.begin(), source.end(), ProcessText.begin());
        ProcessDraft.SetSource(std::string(source));
    };
    if (ImGui::Button("Load three-pulse draft")) load(CarrierExample().Source());
    ImGui::SameLine();
    const auto &active = session.GetFormulaRuntime().ActiveProcess();
    ImGui::BeginDisabled(!active);
    if (ImGui::Button("Load applied process")) load(active->Source());
    ImGui::EndDisabled();
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
    if (active) ImGui::Text("Applied v%llu | %.12s", static_cast<unsigned long long>(session.AppliedProgram().Version),
        active->Identity().ContentSha256.c_str());
    ImGui::TextDisabled("Compile then Apply. Runtime instances and drafts are not saved to disk yet.");
}

void FormulaDebugUI::DrawProcessExample(GameSession &session) {
    if (!ImGui::CollapsingHeader("Process: three carriers")) return;
    ImGui::TextWrapped("Each pulse launches a ball and digs on hit. Natural source, fixed fixture and right-click use the same program. Unpause or Step to advance.");
    const auto *player = session.GetPlayer();
    ImGui::BeginDisabled(!player || session.AppliedProgram().ResourceId != 2);
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
    ImGui::EndDisabled();
    ImGui::TextDisabled("Fixture credits: natural 6, fixed 4, player 64; one credit per accepted effect. Not saved.");
    const auto &bridge = session.GetFormulaRuntime();
    const auto stats = bridge.ProcessStats();
    ImGui::Text("Process instances %zu | events %zu | last boundary work %llu | accepted %llu / refused %llu",
        stats.Instances, stats.PendingEvents, static_cast<unsigned long long>(stats.Work),
        static_cast<unsigned long long>(stats.Accepted), static_cast<unsigned long long>(stats.Rejected));
    for (const auto &diagnostic : bridge.ProcessDiagnostics()) ImGui::TextWrapped("%s", diagnostic.Message.c_str());
    for (const auto &instance : bridge.InspectProcesses()) {
        const auto &state = instance.State;
        const char *phase = instance.Suspended ? "frozen"
            : state.Status == formula::process::Phase::Sleeping ? "wait" : "await";
        ImGui::Text("#%llu v%llu %.12s | %s PC %zu | active %llu remaining %llu | credits %lld",
            static_cast<unsigned long long>(state.Id), static_cast<unsigned long long>(instance.Version),
            instance.Program.ContentSha256.c_str(), phase, state.Instruction,
            static_cast<unsigned long long>(state.ActiveSteps), static_cast<unsigned long long>(state.Remaining),
            static_cast<long long>(instance.Credits));
    }
}

void FormulaDebugUI::DrawTree(const formula::editing::DraftNode &node, const char *slot) {
    const auto identity = std::to_string(node.Id);
    ImGui::PushID(identity.c_str());
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node.Id == Selected) flags |= ImGuiTreeNodeFlags_Selected;
    if (node.Children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    const char *name = node.Kind == NodeKind::Bolt ? "Bolt (base input)" : formula::NodeKindName(node.Kind);
    const bool open = ImGui::TreeNodeEx("node", flags, "%s%s #%llu", slot, name, static_cast<unsigned long long>(node.Id));
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        Selected = node.Id;
    if (open) {
        for (size_t index = 0; index < node.Children.size(); ++index) {
            const std::string label = node.Kind == NodeKind::OnHit ? (index == 0 ? "Carrier: " : "Payload: ") : std::to_string(index + 1) + ": ";
            if (node.Children[index])
                DrawTree(*node.Children[index], label.c_str());
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void FormulaDebugUI::DrawProperties() {
    auto *node = Draft.Find(Selected);
    if (!node) {
        Selected = Draft.Root().Id;
        node = &Draft.Root();
    }
    ImGui::Text("Selected #%llu: %s", static_cast<unsigned long long>(Selected), formula::NodeKindName(node->Kind));
    if (EditParameters(*node)) {
        Dirty = true;
        Status.clear();
        Rebuild();
    }
    ImGui::Separator();
    ImGui::Combo("New node type", &NewKind, KindOptions);
    const auto kind = NodeKind(NewKind);
    // Each command reacquires by stable ID. A mutation may replace vector storage.
    if (ImGui::Button("Replace")) SelectOperation(Draft.Replace(Selected, kind));
    ImGui::SameLine();
    ImGui::BeginDisabled(!CanWrap(kind));
    if (ImGui::Button("Wrap selected")) SelectOperation(Draft.Wrap(Selected, kind));
    ImGui::EndDisabled();
    node = Draft.Find(Selected);
    ImGui::BeginDisabled(!node || node->Kind != NodeKind::Sequence);
    if (ImGui::Button("Add child")) SelectOperation(Draft.AddChild(Selected, kind));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) SelectOperation(Draft.Duplicate(Selected));
    ImGui::SameLine();
    if (ImGui::Button("Delete")) SelectOperation(Draft.Delete(Selected));
    node = Draft.Find(Selected);
    ImGui::BeginDisabled(!node || node->Children.empty());
    if (ImGui::Button("Unwrap / keep first")) SelectOperation(Draft.Unwrap(Selected));
    ImGui::EndDisabled();
    ImGui::SameLine();
    node = Draft.Find(Selected);
    ImGui::BeginDisabled(!node || node->Kind != NodeKind::OnHit);
    if (ImGui::Button("Keep payload")) SelectOperation(Draft.Unwrap(Selected, 1));
    ImGui::EndDisabled();
    if (ImGui::Button("Move up") && Draft.Move(Selected, -1)) SelectOperation(Selected);
    ImGui::SameLine();
    if (ImGui::Button("Move down") && Draft.Move(Selected, 1)) SelectOperation(Selected);
    for (const auto &error : Compiled.Errors) {
        if (error.Node == Selected || error.Node == 0)
            ImGui::TextWrapped("%s: %s", error.Field.c_str(), error.Message.c_str());
    }
}

void FormulaDebugUI::DrawDiagnostics(GameSession &session) {
    const auto &runtime = session.GetFormulaRuntime();
    const auto stats = runtime.Stats();
    ImGui::Text("Roots %zu | balls %zu | queued %zu | reserved effects %llu/256", stats.Roots, stats.Projectiles, stats.Pending, static_cast<unsigned long long>(stats.ReservedEffects));
    ImGui::Text("Wind reserved %llu + retired %llu /64 | calls this frame %llu | casts %llu accepted, %llu refused", static_cast<unsigned long long>(stats.ReservedWind), static_cast<unsigned long long>(stats.RetiredWind), static_cast<unsigned long long>(stats.WindCallsThisFrame), static_cast<unsigned long long>(stats.AcceptedThisFrame), static_cast<unsigned long long>(stats.RejectedThisFrame));
    ImGui::Text("Tracked snapshot capacity %.1f KiB | diagnostics capacity %.1f KiB", double(stats.SnapshotBytes) / 1024, double(stats.DiagnosticBytes) / 1024);
    ImGui::TextDisabled("Estimates exclude simulator/description ownership, deque blocks and allocator overhead.");
    if (!session.GetFormulaError().empty())
        ImGui::TextWrapped("Activation: %s", session.GetFormulaError().c_str());
    if (session.IsPaused())
        ImGui::TextWrapped("Paused: right-click activates at this tick; Step advances motion, delays, repeats and payloads.");
    if (session.GetWorld().GetEnvironment().IsWaitingForBudget())
        ImGui::TextWrapped("Waiting for world storage budget: new casts and terrain edits are refused until retry succeeds.");
    ImGui::TextWrapped("World wind: %s. Pulses need enabled Coarse wind, at most 1024 cells.", session.GetSelectedWindStrategy() == WindStrategyKind::Coarse ? "Coarse" : "unsupported for pulses");
    ImGui::TextDisabled("Draft, recipe and running instances are not saved. Applied terrain changes use world saves.");
    ImGui::BeginChild("Formula events", {0, 140}, true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto &entry : runtime.Diagnostics())
        ImGui::Text("E%llu C%llu t%llu: %s", static_cast<unsigned long long>(entry.WorldEpoch), static_cast<unsigned long long>(entry.CastId), static_cast<unsigned long long>(entry.Tick), entry.Message.c_str());
    ImGui::EndChild();
}

void FormulaDebugUI::Draw(GameSession &session) {
    if (!Visible)
        return;
    if (!Initialized) {
        LoadApplied(session);
        ProcessDraft.SetSource(std::string(CarrierExample().Source()));
        std::copy(ProcessDraft.Source().begin(), ProcessDraft.Source().end(), ProcessText.begin());
        Initialized = true;
    }
    const auto display = ImGui::GetIO().DisplaySize;
    const ImVec2 initialSize{std::min(850.f, std::max(240.f, display.x - 32)),
                            std::min(720.f, std::max(240.f, display.y - 32))};
    ImGui::SetNextWindowPos({std::max(16.f, std::min(400.f, display.x - initialSize.x - 16)), 16}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(initialSize, ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Formula Editor", &Visible, ImGuiWindowFlags_NoSavedSettings)) {
        DrawToolbar(session);
        DrawProcessEditor(session);
        DrawProcessExample(session);
        if (ImGui::BeginTable("Formula structure", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableNextColumn();
            ImGui::BeginChild("Tree", {0, 280}, true);
            DrawTree(Draft.Root());
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("Parameters", {0, 280}, true);
            DrawProperties();
            ImGui::EndChild();
            ImGui::EndTable();
        }
        if (!Compiled.Errors.empty()) {
            ImGui::TextWrapped("Draft has %zu error(s); active version is unchanged.", Compiled.Errors.size());
            for (const auto &error : Compiled.Errors)
                ImGui::TextWrapped("Node #%llu / %s: %s", static_cast<unsigned long long>(error.Node), error.Field.c_str(), error.Message.c_str());
        }
        ImGui::Text("Draft: %zu nodes, depth %zu, %zu effects, %zu visits, end by tick %llu", Compiled.Budget.Nodes, Compiled.Budget.Depth, Compiled.Budget.Effects, Compiled.Budget.NodeVisits, static_cast<unsigned long long>(Compiled.Budget.EndTick));
        ImGui::BeginChild("Read-only DSL preview", {0, 64}, true, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(Preview.c_str());
        ImGui::EndChild();
        DrawDiagnostics(session);
    }
    ImGui::End();
}
} // namespace pixel
