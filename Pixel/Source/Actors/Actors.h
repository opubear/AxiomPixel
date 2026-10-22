#pragma once
#include "Character.h"
#include "PlayerInput.h"
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
enum class ActorKind { Player, Resident };
struct ActorRecord {
    ActorId Id;
    ActorKind Kind;
    Character Body;
};
// Presentation/observation values contain no velocity state or storage handle.
struct ActorSnapshot {
    ActorId Id;
    ActorKind Kind;
    WorldPosition Position;
    bool Grounded = false;
    WorldPosition GetWorldPosition() const { return Position; }
};

class Actors {
  public:
    static constexpr size_t MaximumCount = 256;
    Actors();
    Actors(const Actors &) = delete;
    Actors &operator=(const Actors &) = delete;

    std::optional<ActorHandle> Spawn(ActorKind kind, WorldPosition position);
    // Trusted restore boundary; record identity survives while the slot and
    // generation are newly assigned. Duplicate IDs/kinds are programming errors.
    std::optional<ActorHandle> Restore(const ActorRecord &record);
    bool Remove(ActorHandle handle);
    void Clear();
    void SetPlayerPosition(WorldPosition position);
    void Step(WorldMapView terrain, PlayerInput input);
    const ActorRecord *Find(ActorHandle handle) const;
    const ActorRecord *Find(ActorId id) const;
    std::optional<ActorHandle> Resolve(ActorId id) const;
    const ActorRecord &Require(ActorHandle handle) const;
    const Character *Player() const;
    std::optional<ActorSnapshot> PlayerSnapshot() const;
    bool Occupies(CellCoord cell) const;
    size_t Count() const { return LiveCount; }
    uint64_t Revision() const { return ChangeRevision; }

  private:
    struct Slot { uint64_t Generation = 1; std::optional<ActorRecord> Actor; };
    std::vector<Slot> Slots;
    uint64_t Owner, NextId = 1, ChangeRevision = 0;
    size_t LiveCount = 0;
    std::optional<ActorHandle> PlayerHandle;
    std::optional<ActorHandle> Insert(ActorRecord record);
    void Changed();
};
} // namespace pixel
