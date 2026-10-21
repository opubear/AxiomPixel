#pragma once

#include "World/Environment/Wind/WindStrategy.h"
#include "World/Generation/WorldGenerator.h"
#include "World/WorldCoordinates.h"

namespace pixel {
// The v4 metadata value contract. Legacy v2/v3 files are decoded into this form;
// chunks remain owned by WorldStreamer, and live Formula instances are not saved.
struct SessionSaveData {
    static constexpr uint32_t CurrentVersion = 4;

    uint32_t WindowEdge = storage::DefaultWindowEdge;
    uint64_t BudgetBytes = storage::DefaultBudget;
    WorldGenerationSettings Generation;
    WorldPosition Player;
    double SimulationTime = 0;
    double PhysicsTime = 0;
    double TimeRemainder = 0;
    bool DeferredStep = false;
    int64_t WindBoundary = 0;
    bool WindEnabled = false;
    bool Paused = true;
    WindStrategySettings Wind;
};
} // namespace pixel
