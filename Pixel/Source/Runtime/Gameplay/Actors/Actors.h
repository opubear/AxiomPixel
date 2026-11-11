#pragma once
#include "Actor.h"

namespace pixel {
class Actors {
  public:
    static constexpr size_t MaximumCount = 256;
    explicit Actors(std::shared_ptr<const MonsterCatalog> monsters = {});
    Actors(const Actors &) = delete;
    Actors &operator=(const Actors &) = delete;

    std::optional<ActorHandle> Spawn(ActorKind kind, WorldPosition position);
    std::optional<ActorHandle> SpawnMonster(std::string_view definition, WorldPosition position);
    bool Damage(ActorId actor, float amount);
    // Trusted restore boundary; record identity survives while the slot and
    // generation are newly assigned. Duplicate IDs/kinds are programming errors.
    std::optional<ActorHandle> Restore(const ActorRecord &record);
    bool Remove(ActorHandle handle);
    void Clear();
    void SetPlayerPosition(WorldPosition position);
    void EquipFormula(ActorId actor, FormulaEquipment equipment);
    void Step(WorldMapView terrain, PlayerInput input);
    // Queries return self-contained state values, never component owners.
    std::optional<ActorRecord> Find(ActorHandle handle) const;
    std::optional<ActorRecord> Find(ActorId id) const;
    std::optional<ActorHandle> Resolve(ActorId id) const;
    ActorRecord Require(ActorHandle handle) const;
    // Synchronous read-only runtime access. Do not retain across removal/reset.
    const Actor *FindActor(ActorHandle handle) const;
    const Actor *FindActor(ActorId id) const;
    const Actor *PlayerActor() const;
    const Body *Player() const;
    std::optional<ActorSnapshot> PlayerSnapshot() const;
    std::vector<ActorSnapshot> Snapshot() const;
    bool Occupies(CellCoord cell) const;
    size_t Count() const { return LiveCount; }
    uint64_t Revision() const { return ChangeRevision; }
    ActorsState CaptureState() const;
    void RestoreState(const ActorsState &state);

  private:
    struct Slot {
        uint64_t Generation = 1;
        std::unique_ptr<Actor> Object;
    };
    std::vector<Slot> Slots;
    std::shared_ptr<const MonsterCatalog> Monsters;
    uint64_t Owner, NextId = 1, ChangeRevision = 0;
    size_t LiveCount = 0;
    std::optional<ActorHandle> PlayerHandle;
    std::optional<ActorHandle> Insert(ActorRecord record);
    void Changed();
};
} // namespace pixel
