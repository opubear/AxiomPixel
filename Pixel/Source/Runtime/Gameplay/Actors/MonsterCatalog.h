#pragma once
#include "Runtime/Gameplay/Ecology/BiomePopulation.h"
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace pixel {
struct MonsterDefinition {
    std::string Id;
    std::array<float, 2> BodySizeCells{2, 4};
    bool Flying = false;
    float SpeedCellsPerSecond = 4, MaxHealth = 30, Damage = 5;
    float DetectionCells = 12, AttackRangeCells = 2, PatrolCells = 6;
    uint32_t AttackWindupTicks = 20, AttackDurationTicks = 40, AttackCooldownTicks = 70;
    uint32_t HurtTicks = 18, DeathTicks = 60, WakeTicks = 0;
    BiomeSpawnWeights BiomeWeights{100, 100, 100};
};
struct MonsterSpawn {
    std::string Id;
    int OffsetCells = 0;
    uint32_t HeightAboveGroundCells = 0;
};

// Gameplay reads only behavior fields. The composition root supplies Content;
// neither actors nor Simulation load images or depend on Render.
struct MonsterCatalog {
    std::vector<MonsterDefinition> Definitions;
    std::vector<MonsterSpawn> Spawns;
    const MonsterDefinition &Require(std::string_view id) const;
    void Validate() const;
    static std::shared_ptr<const MonsterCatalog> Parse(const std::vector<char> &json);
};
} // namespace pixel
