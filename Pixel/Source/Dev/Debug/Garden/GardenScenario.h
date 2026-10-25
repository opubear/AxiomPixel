#pragma once
#include "ForceGarden.h"
#include "Runtime/Gameplay/Actors/Body.h"
#include "Runtime/Gameplay/FormulaBridge/FormulaBridge.h"

namespace pixel::debug {
// Opt-in integration scenario. The session supplies the domains and decides
// when a successful fixed step or checkpoint is allowed to publish its state.
void InitializeForceGarden(World &world, FormulaBridge &formulas, ForceGardenState &garden, const Body &player);
GardenSample SampleGarden(const World &world, const ForceGardenState &garden, int64_t x, uint32_t growth);
void StepForceGarden(World &world, FormulaBridge &formulas, ForceGardenState &garden,
                    std::vector<FormulaEffectRecord> &effects);
} // namespace pixel::debug
