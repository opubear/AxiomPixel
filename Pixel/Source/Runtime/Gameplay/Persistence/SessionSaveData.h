#pragma once

#include "Runtime/Simulation/World/Environment/Wind/WindStrategy.h"
#include "Runtime/Simulation/World/Generation/WorldGenerator.h"
#include "Runtime/Simulation/World/WorldCoordinates.h"
#include "Runtime/Simulation/World/Environment/WorldCalendar.h"

namespace pixel {
// The v7 world/configuration part of checkpoint/1. Legacy v2-v6 files decode into
// this form; GameplaySaveData carries actors, inventory and runtime state.
struct SessionSaveData {
    static constexpr uint32_t CurrentVersion = 7;

    uint32_t WindowEdge = storage::DefaultWindowEdge;
    uint64_t BudgetBytes = storage::DefaultBudget;
    WorldGenerationSettings Generation;
    WorldPosition Player;
    double CommittedTime = 0;
    double TimeRemainder = 0;
    // v2-v7 format slot: read for compatibility, never used to clip wind.
    int64_t LegacyWindBoundary = 0;
    bool WindEnabled = false;
    bool Paused = false;
    WorldCalendarState Calendar;
    WindStrategySettings Wind;
};
} // namespace pixel
