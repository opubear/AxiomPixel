#pragma once

#include "World/Environment/Wind/WindStrategy.h"
#include "World/Generation/WorldGenerator.h"
#include "World/WorldCoordinates.h"

namespace pixel {
// The v5 committed metadata contract. Legacy v2-v4 files are decoded into this form;
// chunks remain owned by WorldStreamer, and live Formula instances are not saved.
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
