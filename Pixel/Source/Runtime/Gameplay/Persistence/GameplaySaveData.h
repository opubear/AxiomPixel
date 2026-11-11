#pragma once
#include "Runtime/Gameplay/Actors/Actors.h"
#include "Runtime/Gameplay/Ecology/RegionalPopulation.h"
#include "Runtime/Gameplay/Devices/Devices.h"
#include "Dev/Debug/Garden/ForceGarden.h"
#include "Runtime/Simulation/World/Environment/FieldSources.h"
#include "Runtime/Gameplay/Authority/AuthorityState.h"
#include "Runtime/Gameplay/Knowledge/KnowledgeTypes.h"
#include "Runtime/Gameplay/GameAPI/WorldWillState.h"
#include "Runtime/Gameplay/Items/DroppedItem.h"
#include <string>
#include <string_view>

namespace pixel {
// Domain values from one complete commit. No in-memory slots, pointers, or
// observation leases survive a checkpoint; program state has its own schema.
struct GameplaySaveData {
    uint64_t Tick = 0, Revision = 1;
    items::ContainerId PlayerBag;
    std::array<double, 2> LastAim{1, 0};
    ActorsState Actors;
    items::InventoryState Inventory;
    DevicesState Devices;
    FieldSourcesState FieldSources;
    std::string Programs;
    ForceGardenState Garden;
    authority::SaveData Authority;
    knowledge::SaveData Knowledge;
    api::WorldWillMemory WorldWill;
    RegionalPopulationState Population;
    items::ContainerId GroundBag;
    std::vector<items::DroppedItem> DroppedItems;
};
std::string EncodeGameplayState(const GameplaySaveData &state);
GameplaySaveData DecodeGameplayState(std::string_view payload);
} // namespace pixel
