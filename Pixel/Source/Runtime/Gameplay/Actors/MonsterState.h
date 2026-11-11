#pragma once
#include "Body.h"
#include <string>

namespace pixel {
enum class MonsterAction { Dormant, Idle, Move, Wake, Attack, Hurt, Death };
const char *MonsterActionName(MonsterAction action);
struct MonsterState {
    std::string Definition;
    WorldPosition Home;
    MonsterAction Action = MonsterAction::Idle;
    uint32_t ActionTicks = 0, CooldownTicks = 0;
    bool FacingLeft = false;
};
} // namespace pixel
