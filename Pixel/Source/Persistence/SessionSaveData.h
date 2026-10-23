#pragma once

#include "World/Environment/Wind/WindStrategy.h"
#include "World/Generation/WorldGenerator.h"
#include "World/WorldCoordinates.h"

namespace pixel {
// The v5 world/configuration part of checkpoint/1. Legacy v2-v4 files decode into
// this form; GameplaySaveData carries actors, inventory and runtime state.
struct SessionSaveData {
    static constexpr uint32_t CurrentVersion = 5;

    uint32_t WindowEdge = storage::DefaultWindowEdge;
    uint64_t BudgetBytes = storage::DefaultBudget;
    WorldGenerationSettings Generation;
    WorldPosition Player;
    double CommittedTime = 0;
    double TimeRemainder = 0;
    int64_t WindBoundary = 0;
    bool WindEnabled = false;
    bool Paused = true;
    WindStrategySettings Wind;
};
} // namespace pixel
