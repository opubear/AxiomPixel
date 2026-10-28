#pragma once

#include "Runtime/Gameplay/FormulaBridge/ProcessCarrier.h"
#include "Runtime/Gameplay/GameAPI/Actions.h"
#include <span>
#include <string_view>

namespace pixel {
class GameSession;

struct FormulaPreset {
    const char *Resource;
    const char *Name;
    const char *Description;
    const char *Hint;
    std::string_view Source;
    ProcessBindingKind Binding = ProcessBindingKind::World;
    std::array<double, 2> Offset{4, 3};
    float AngleDegrees = 35;
    bool WaitsForWake = false;
};

std::span<const FormulaPreset> FormulaPresets();
const formula::process::ProgramArtifact &FormulaPresetProgram(size_t index);
uint64_t FormulaPresetSubject(size_t index);
// Publishes the preset for subsequent right-click casts and restarts only this
// preset's fixture. Every world mutation goes through the native Session API.
api::ControlReceipt RunFormulaPreset(GameSession &session, size_t index, double angleDegrees);
api::ControlReceipt StopFormulaPreset(GameSession &session, size_t index);
api::ControlReceipt WakeFormulaPreset(GameSession &session, size_t index);
} // namespace pixel
