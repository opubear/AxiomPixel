#include "Actors.h"
#include "BodyComponent.h"
#include "MonsterComponent.h"
#include "HealthComponent.h"
#include "Dev/Debug/Performance/FrameProfiler.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace pixel {
namespace {
Body BodyAt(WorldPosition position) {
    position.Normalize();
    Body body;
    body.OriginChunk = position.Chunk;
    body.X = position.Local[0];
    body.Y = position.Local[1];
    return body;
}
uint64_t NewOwner() {
    static std::atomic<uint64_t> next{1};
    const auto owner = next.fetch_add(1);
    if (owner == UINT64_MAX) std::terminate();
    return owner;
}
} // namespace

Actors::Actors(std::shared_ptr<const MonsterCatalog> monsters) : Monsters(std::move(monsters)), Owner(NewOwner()) {
    if (Monsters) Monsters->Validate();
}

ActorsState Actors::CaptureState() const {
    ActorsState state{NextId};
    for (const auto &slot : Slots)
        if (slot.Object) state.Records.push_back(slot.Object->CaptureState());
    return state;
}

void Actors::RestoreState(const ActorsState &state) {
    if (!state.NextId || state.Records.size() > MaximumCount)
        throw std::invalid_argument("invalid actors checkpoint");
    Actors candidate(Monsters);
    for (const auto &record : state.Records) {
        if (record.Id.Value >= state.NextId || (record.Kind == ActorKind::Player && candidate.Player()))
            throw std::invalid_argument("invalid actors checkpoint identity");
        candidate.Restore(record);
    }
    Slots = std::move(candidate.Slots);
    Owner = candidate.Owner;
    PlayerHandle = candidate.PlayerHandle;
    LiveCount = candidate.LiveCount;
    NextId = state.NextId;
    Changed();
}

void Actors::Changed() {
    if (ChangeRevision == UINT64_MAX) throw std::overflow_error("actor revision exhausted");
    ++ChangeRevision;
}

std::optional<ActorHandle> Actors::Insert(ActorRecord record) {
    // Construct components before changing slot occupancy or identity counters.
    auto actor = std::make_unique<Actor>(record, Monsters);
    if (record.Kind == ActorKind::Player && PlayerHandle)
        throw std::logic_error("only one controlled player is supported");
    if (LiveCount == MaximumCount) return std::nullopt;

    size_t index = 0;
    while (index < Slots.size() && Slots[index].Object) ++index;
    if (index == Slots.size()) Slots.emplace_back();
    auto &slot = Slots[index];
    const ActorHandle handle{uint32_t(index), slot.Generation, Owner};
    slot.Object = std::move(actor);
    if (record.Kind == ActorKind::Player) PlayerHandle = handle;
    ++LiveCount;
    Changed();
    return handle;
}

std::optional<ActorHandle> Actors::Spawn(ActorKind kind, WorldPosition position) {
    if (NextId == UINT64_MAX) throw std::overflow_error("actor identity exhausted");
    ActorRecord record{{NextId}, kind, BodyAt(position)};
    if (kind == ActorKind::Player && Monsters) record.Health = HealthState{100, 100, 0, position};
    auto result = Insert(std::move(record));
    if (result) ++NextId;
    return result;
}

std::optional<ActorHandle> Actors::SpawnMonster(std::string_view definition, WorldPosition position) {
    if (!Monsters) throw std::logic_error("monster spawning requires a catalog");
    if (NextId == UINT64_MAX) throw std::overflow_error("actor identity exhausted");
    const auto &d = Monsters->Require(definition);
    auto body = BodyAt(position);
    ActorRecord record{{NextId}, ActorKind::Monster, body};
    record.Monster = MonsterState{d.Id, body.GetWorldPosition(), d.WakeTicks ? MonsterAction::Dormant : MonsterAction::Idle};
    record.Health = HealthState{d.MaxHealth, d.MaxHealth, 0, body.GetWorldPosition()};
    auto result = Insert(std::move(record));
    if (result) ++NextId;
    return result;
}

bool Actors::Damage(ActorId actor, float amount) {
    if (!std::isfinite(amount) || amount < 0) throw std::invalid_argument("invalid actor damage");
    const auto handle = Resolve(actor);
    if (!handle || !Slots[handle->Slot].Object->TakeDamage(amount)) return false;
    Changed();
    return true;
}

std::optional<ActorHandle> Actors::Restore(const ActorRecord &record) {
    if (record.Id.Value == 0 || record.Id.Value == UINT64_MAX || Find(record.Id))
        throw std::invalid_argument("invalid or duplicate persistent actor identity");
    auto normalized = record.Body.GetWorldPosition();
    normalized.Normalize();
    if (normalized.Chunk != record.Body.OriginChunk || normalized.Local != record.Body.GetWorldPosition().Local)
        throw std::invalid_argument("actor record position is not normalized");
    auto restored = record;
    // Pre-combat checkpoints gain player health at their saved position; they
    // never acquire replacement monsters or duplicate existing identities.
    if (Monsters && restored.Kind == ActorKind::Player && !restored.Health)
        restored.Health = HealthState{100, 100, 0, restored.Body.GetWorldPosition()};
    const auto result = Insert(std::move(restored));
    if (result) NextId = std::max(NextId, record.Id.Value + 1);
    return result;
}

bool Actors::Remove(ActorHandle handle) {
    if (!FindActor(handle)) return false;
    auto &slot = Slots[handle.Slot];
    if (slot.Generation == UINT64_MAX) throw std::overflow_error("actor generation exhausted");
    slot.Object.reset();
    ++slot.Generation;
    if (PlayerHandle == handle) PlayerHandle.reset();
    --LiveCount;
    Changed();
    return true;
}

void Actors::Clear() {
    for (uint32_t index = 0; index < Slots.size(); ++index)
        if (Slots[index].Object) Remove({index, Slots[index].Generation, Owner});
}

std::optional<ActorRecord> Actors::Find(ActorHandle handle) const {
    const auto *actor = FindActor(handle);
    return actor ? std::optional{actor->CaptureState()} : std::nullopt;
}

std::optional<ActorHandle> Actors::Resolve(ActorId id) const {
    if (id.Value == 0) return std::nullopt;
    for (uint32_t index = 0; index < Slots.size(); ++index)
        if (Slots[index].Object && Slots[index].Object->GetId() == id)
            return ActorHandle{index, Slots[index].Generation, Owner};
    return std::nullopt;
}

std::optional<ActorRecord> Actors::Find(ActorId id) const {
    const auto handle = Resolve(id);
    return handle ? Find(*handle) : std::nullopt;
}

ActorRecord Actors::Require(ActorHandle handle) const {
    const auto actor = Find(handle);
    if (!actor) throw std::logic_error("expired internal actor handle");
    return *actor;
}

const Actor *Actors::FindActor(ActorHandle handle) const {
    if (handle.Owner != Owner || handle.Slot >= Slots.size()) return nullptr;
    const auto &slot = Slots[handle.Slot];
    return slot.Generation == handle.Generation ? slot.Object.get() : nullptr;
}

const Actor *Actors::FindActor(ActorId id) const {
    const auto handle = Resolve(id);
    return handle ? FindActor(*handle) : nullptr;
}

const Actor *Actors::PlayerActor() const {
    return PlayerHandle ? FindActor(*PlayerHandle) : nullptr;
}

const Body *Actors::Player() const {
    const auto *actor = PlayerActor();
    return actor ? &actor->GetComponent<BodyComponent>()->GetBody() : nullptr;
}

std::optional<ActorSnapshot> Actors::PlayerSnapshot() const {
    if (!PlayerHandle) return std::nullopt;
    const auto &actor = Require(*PlayerHandle);
    ActorSnapshot snapshot{actor.Id, actor.Kind, actor.Body.GetWorldPosition(), actor.Body.Grounded};
    if (actor.Health) { snapshot.Health = actor.Health->Current; snapshot.MaxHealth = actor.Health->Maximum; }
    return snapshot;
}

std::vector<ActorSnapshot> Actors::Snapshot() const {
    std::vector<ActorSnapshot> result;
    result.reserve(LiveCount);
    for (const auto &slot : Slots) {
        if (!slot.Object) continue;
        const auto actor = slot.Object->CaptureState();
        result.push_back({actor.Id, actor.Kind, actor.Body.GetWorldPosition(), actor.Body.Grounded,
            {CentimetersToCells(actor.Body.Size[0]), CentimetersToCells(actor.Body.Size[1])}, actor.Monster,
            actor.Health ? actor.Health->Current : 0, actor.Health ? actor.Health->Maximum : 0});
    }
    return result;
}

void Actors::SetPlayerPosition(WorldPosition position) {
    auto body = BodyAt(position);
    if (PlayerHandle) {
        auto &actor = *Slots[PlayerHandle->Slot].Object;
        actor.GetComponent<BodyComponent>()->SetPosition(body.GetWorldPosition());
        Changed();
    } else if (!Spawn(ActorKind::Player, position)) {
        throw std::logic_error("no capacity for the session player");
    }
}

void Actors::EquipFormula(ActorId actor, FormulaEquipment equipment) {
    const auto handle = Resolve(actor);
    if (!handle || !equipment.Program.ResourceId || !equipment.Program.Version)
        throw std::invalid_argument("formula equipment requires a live actor and program");
    Slots[handle->Slot].Object->EquipFormula(equipment);
    Changed();
}

void Actors::Step(WorldMapView terrain, PlayerInput input) {
    PIXEL_PROFILE_SCOPE(Character);
    bool advanced = false;
    for (auto &slot : Slots) {
        if (!slot.Object) continue;
        if (auto *monster = slot.Object->GetComponent<MonsterComponent>()) {
            // Copy the target before any strike/respawn can move it. Every attack
            // rechecks range and terrain at its hit tick, not at windup start.
            const auto target = Player() ? std::optional<Body>{*Player()} : std::nullopt;
            const auto step = monster->Step(terrain, target);
            advanced |= step.Advanced;
            if (step.Advanced) slot.Object->GetComponent<HealthComponent>()->Step();
            if (step.Strike && PlayerHandle) Damage(Slots[PlayerHandle->Slot].Object->GetId(), monster->GetDefinition().Damage);
            if (step.Expired) Remove(*Resolve(slot.Object->GetId()));
            continue;
        }
        const auto intent = slot.Object->GetKind() == ActorKind::Player ? input : PlayerInput{};
        if (slot.Object->Step(terrain, intent)) advanced = true;
    }
    if (advanced) Changed();
}

bool Actors::Occupies(CellCoord cell) const {
    for (const auto &slot : Slots)
        if (slot.Object && slot.Object->GetComponent<BodyComponent>()->GetBody().Occupies(cell.X, cell.Y)) return true;
    return false;
}
} // namespace pixel
