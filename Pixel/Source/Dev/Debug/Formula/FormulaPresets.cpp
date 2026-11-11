#include "FormulaPresets.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"

namespace pixel {
namespace {
constexpr std::array Presets{
    FormulaPreset{"debug/formula/mining-volley/1", "Mining volley",
        "Three mining shots: a reusable impact function wrapped in repeat.",
        "Aim down at nearby terrain. Each hit digs a 2-cell radius; the next shot waits 20 ticks.",
        R"(formula 2;
fn mining_shot(velocity: speed, radius: cells) -> action =
    on_hit(bolt(10, velocity, ticks(90)), dig(radius));
fn volley(count: number, shot: action) -> action =
    repeat(count, ticks(20), shot);
run volley(3, mining_shot(speed(40), cells(2)));
)"},
    FormulaPreset{"debug/formula/delayed-excavation/1", "Delayed excavation",
        "A hit plants a delayed, two-stage excavation at the impact point.",
        "Aim at nearby terrain, then move away: the 1-cell cut and later 4-cell cut stay at the hit.",
        R"(formula 2;
fn staged_cut(radius: cells) -> action =
    sequence(dig(cells(1)), delay(ticks(45), dig(radius)));
fn plant(payload: action) -> action =
    on_hit(bolt(0, speed(40), ticks(90)), delay(ticks(30), payload));
run plant(staged_cut(cells(4)));
)"},
    FormulaPreset{"debug/formula/alternating-force/1", "Alternating force",
        "Two right/left force cycles, composed from finite-duration fields.",
        "Press F1 to see the field. It pushes along your aim for 90 ticks, then in the opposite direction for 90.",
        R"(formula 2;
fn push(strength: acceleration) -> action =
    field(cells(8), strength, acceleration(0), ticks(90));
fn cycle() -> action =
    sequence(push(acceleration(3600)), push(acceleration(-3600)));
run repeat(2, ticks(30), cycle());
)"},
    FormulaPreset{"debug/formula/walking-beacon/1", "Beacon pulses",
        "Six stationary yellow beacons, emitted at the launch point.",
        "Left-click to start the pulses. The launch point stays fixed while you move.",
        R"(formula 2;
fn beacon(lifetime: ticks) -> action = bolt(0, speed(0), lifetime);
fn blink(count: number, light: action) -> action =
    repeat(count, ticks(15), light);
run blink(6, beacon(ticks(45)));
)"},
    FormulaPreset{"debug/formula/wake-launcher/1", "Wake launcher",
        "An event-driven launcher: it waits for Wake player formulas before firing.",
        "Left-click, then press Wake player formulas within 600 active ticks. Timeout ends without firing.",
        R"(formula 2;
fn mining_shot(radius: cells) -> action =
    on_hit(bolt(10, speed(40), ticks(90)), dig(radius));
fn armed(payload: action) -> action = on_wake(ticks(600), payload);
run armed(mining_shot(cells(3)));
)", true}
};
} // namespace

std::span<const FormulaPreset> FormulaPresets() { return Presets; }

const formula::process::ProgramArtifact &FormulaPresetProgram(size_t index) {
    static const auto programs = [] {
        std::vector<formula::process::ProgramArtifact> result;
        result.reserve(Presets.size());
        for (const auto &preset : Presets)
            result.push_back(std::get<formula::process::ProgramArtifact>(
                formula::process::Compile(preset.Resource, preset.Source)));
        return result;
    }();
    return programs.at(index);
}

api::ControlReceipt LoadFormulaPreset(GameSession &session, size_t index) {
    if (!session.GetActors().PlayerSnapshot())
        return {api::ControlStatus::Rejected, api::Reason::NotReady, "A player is required to equip a formula."};
    const auto applied = session.ApplyProcess(NativeCaller::Debug, FormulaPresetProgram(index));
    if (!applied) return {api::ControlStatus::Rejected, applied.Rejection, applied.Message};
    return {api::ControlStatus::Applied};
}

api::ControlReceipt StopPlayerFormulas(GameSession &session) {
    const auto player = session.GetActors().PlayerSnapshot();
    if (!player) return {api::ControlStatus::Rejected, api::Reason::NotReady, "Player unavailable"};
    return session.CancelProcessCarrier(NativeCaller::Debug, ProcessCarrierKind::Player, player->Id.Value);
}

api::ControlReceipt WakePlayerFormulas(GameSession &session) {
    const auto player = session.GetActors().PlayerSnapshot();
    if (!player) return {api::ControlStatus::Rejected, api::Reason::NotReady, "Player unavailable"};
    return session.WakeProcessCarrier(NativeCaller::Debug, ProcessCarrierKind::Player, player->Id.Value);
}
} // namespace pixel
