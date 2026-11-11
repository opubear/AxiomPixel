#include "MonsterCatalog.h"
#include "Asset/Parsers/JsonParser.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace pixel {
const MonsterDefinition &MonsterCatalog::Require(std::string_view id) const {
    for (const auto &definition : Definitions)
        if (definition.Id == id) return definition;
    throw std::invalid_argument("unknown monster definition: " + std::string(id));
}

void MonsterCatalog::Validate() const {
    if (Definitions.empty() || Definitions.size() > 32 || Spawns.size() > 64)
        throw std::invalid_argument("monsters require 1..32 definitions and at most 64 spawns");
    std::unordered_set<std::string> ids;
    const auto number = [](float value, float low, float high) {
        return std::isfinite(value) && value >= low && value <= high;
    };
    for (const auto &d : Definitions) {
        ValidateBiomeSpawnWeights(d.BiomeWeights);
        if (d.Id.empty() || d.Id.size() > 32 || d.Id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos
            || !ids.insert(d.Id).second || !number(d.BodySizeCells[0], .5f, 8) || !number(d.BodySizeCells[1], .5f, 8)
            || !number(d.SpeedCellsPerSecond, .1f, 32) || !number(d.MaxHealth, 1, 1e6f) || !number(d.Damage, 0, 1e6f)
            || !number(d.DetectionCells, 1, 64) || !number(d.AttackRangeCells, .1f, 8)
            || !number(d.PatrolCells, 1, 32) || !d.AttackWindupTicks || d.AttackWindupTicks >= d.AttackDurationTicks
            || d.AttackDurationTicks > 600 || d.AttackCooldownTicks < d.AttackDurationTicks || d.AttackCooldownTicks > 3600
            || !d.HurtTicks || d.HurtTicks > 600 || !d.DeathTicks || d.DeathTicks > 3600 || d.WakeTicks > 600)
            throw std::invalid_argument("invalid monster behavior: " + d.Id);
    }
    for (const auto &spawn : Spawns) {
        Require(spawn.Id);
        if (spawn.OffsetCells < -1024 || spawn.OffsetCells > 1024 || spawn.HeightAboveGroundCells > 32)
            throw std::invalid_argument("monster spawn exceeds supported range");
    }
}

std::shared_ptr<const MonsterCatalog> MonsterCatalog::Parse(const std::vector<char> &json) {
    auto result = std::make_shared<MonsterCatalog>();
    JsonParser::Parse(json, [&](const auto &document) {
        if (!document.at("Version").is_number_integer() || document.at("Version") != 1)
            throw std::invalid_argument("unsupported Monsters.json Version");
        const auto &definitions = document.at("Monsters"), &spawns = document.at("Spawns");
        if (!definitions.is_array() || definitions.size() > 32 || !spawns.is_array() || spawns.size() > 64)
            throw std::invalid_argument("invalid monster catalog arrays");
        const auto ticks = [](const auto &value) -> uint32_t {
            if (!value.is_number_integer() || value < 0 || value > 3600)
                throw std::invalid_argument("monster ticks must be integers in [0,3600]");
            return value.template get<uint32_t>();
        };
        for (const auto &entry : definitions) {
            const auto &g = entry.at("Gameplay");
            MonsterDefinition d;
            d.Id = entry.at("Id").template get<std::string>();
            if (!g.at("BodySizeCells").is_array() || g.at("BodySizeCells").size() != 2)
                throw std::invalid_argument("monster body requires exactly two dimensions");
            d.BodySizeCells = g.at("BodySizeCells").template get<std::array<float, 2>>();
            d.Flying = g.at("Flying").template get<bool>();
            d.SpeedCellsPerSecond = g.at("SpeedCellsPerSecond").template get<float>();
            d.MaxHealth = g.at("MaxHealth").template get<float>();
            d.Damage = g.at("Damage").template get<float>();
            d.DetectionCells = g.at("DetectionCells").template get<float>();
            d.AttackRangeCells = g.at("AttackRangeCells").template get<float>();
            d.PatrolCells = g.at("PatrolCells").template get<float>();
            d.AttackWindupTicks = ticks(g.at("AttackWindupTicks"));
            d.AttackDurationTicks = ticks(g.at("AttackDurationTicks"));
            d.AttackCooldownTicks = ticks(g.at("AttackCooldownTicks"));
            d.HurtTicks = ticks(g.at("HurtTicks"));
            d.DeathTicks = ticks(g.at("DeathTicks"));
            d.WakeTicks = ticks(g.at("WakeTicks"));
            if (g.contains("BiomeWeights")) {
                const auto &weights = g.at("BiomeWeights");
                if (!weights.is_array() || weights.size() != 3)
                    throw std::invalid_argument("monster BiomeWeights requires forest, desert and snow percentages");
                for (size_t i = 0; i < 3; ++i) {
                    if (!weights[i].is_number_integer() || weights[i] < 0 || weights[i] > 100)
                        throw std::invalid_argument("monster BiomeWeights must be integer percentages");
                    d.BiomeWeights[i] = weights[i].template get<uint32_t>();
                }
            }
            result->Definitions.push_back(std::move(d));
        }
        for (const auto &entry : spawns) {
            const auto &offset = entry.at("OffsetCells");
            if (!offset.is_number_integer() || offset < -1024 || offset > 1024)
                throw std::invalid_argument("monster spawn offset must be an integer in [-1024,1024]");
            result->Spawns.push_back({entry.at("Id").template get<std::string>(), offset.template get<int>(),
                ticks(entry.at("HeightAboveGroundCells"))});
        }
    });
    result->Validate();
    return result;
}
} // namespace pixel
