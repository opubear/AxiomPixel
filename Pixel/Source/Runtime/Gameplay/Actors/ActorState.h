#pragma once
#include "Body.h"
#include "HealthState.h"
#include "MonsterState.h"
#include "Runtime/Gameplay/GameAPI/ProgramRef.h"
#include <optional>
#include <vector>

namespace pixel {
// Durable identity within a saved world. External references also need that
// world's ID/epoch; an in-memory slot is never written as an actor identity.
struct ActorId {
    uint64_t Value = 0;
    bool operator==(const ActorId &) const = default;
};
struct ActorHandle {
    uint32_t Slot = UINT32_MAX;
    uint64_t Generation = 0, Owner = 0;
    bool operator==(const ActorHandle &) const = default;
};
enum class ActorKind { Player, Resident, TrainingDummy, Monster };

struct FormulaEquipment {
    api::ProgramRef Program;
};

// Copyable state, separate from Actor/Component ownership and runtime pointers.
struct ActorRecord {
    ActorId Id;
    ActorKind Kind;
    pixel::Body Body;
    std::optional<FormulaEquipment> Formula;
    std::optional<MonsterState> Monster;
    std::optional<HealthState> Health;
};
struct ActorsState {
    uint64_t NextId = 1;
    std::vector<ActorRecord> Records;
};

// Presentation/observation values contain no velocity state or storage handle.
struct ActorSnapshot {
    ActorId Id;
    ActorKind Kind;
    WorldPosition Position;
    bool Grounded = false;
    std::array<float, 2> SizeCells{Body::WidthCells, Body::HeightCells};
    std::optional<MonsterState> Monster;
    float Health = 0, MaxHealth = 0;
    WorldPosition GetWorldPosition() const { return Position; }
};
} // namespace pixel
