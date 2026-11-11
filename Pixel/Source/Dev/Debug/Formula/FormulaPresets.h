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
    bool WaitsForWake = false;
};

std::span<const FormulaPreset> FormulaPresets();
const formula::process::ProgramArtifact &FormulaPresetProgram(size_t index);
// Native fixture workbench; player-facing editing uses ApplyHeldProcess.
api::ControlReceipt LoadFormulaPreset(GameSession &session, size_t index);
api::ControlReceipt StopPlayerFormulas(GameSession &session);
api::ControlReceipt WakePlayerFormulas(GameSession &session);
} // namespace pixel
