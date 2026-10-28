#include "Actors.h"
#include "Dev/Debug/Performance/FrameProfiler.h"
#include <algorithm>
#include <atomic>
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
// A moving simulation window may cut through an existing body. Unknown terrain
// must freeze that actor, not enter Body's solid-overlap escape path.
bool HasBodyTerrain(const Body &body, WorldMapView terrain) {
    const auto first = body.GetWorldPosition().Cell();
    const auto origin = body.CellOrigin();
    const CellCoord last{origin.X + int64_t(std::ceil(body.X + Body::Width)) - 1,
                         origin.Y + int64_t(std::ceil(body.Y + Body::Height)) - 1};
    const WorldRect active{terrain.Origin.X, terrain.Origin.Y, terrain.Width, terrain.Height};
    if (!active.Contains(first) || !active.Contains(last)) return false;
    for (auto y = first.Y; y <= last.Y; ++y)
        for (auto x = first.X; x <= last.X; ++x)
            if (!terrain.TryAtLocal(uint32_t(x - active.X), uint32_t(y - active.Y))) return false;
    return true;
}

uint64_t NewOwner() {
    static std::atomic<uint64_t> next{1};
    const auto owner = next.fetch_add(1);
    if (owner == UINT64_MAX) std::terminate();
    return owner;
}
} // namespace

Actors::Actors() : Owner(NewOwner()) {}

ActorsState Actors::CaptureState() const {
    ActorsState state{NextId};
    for (const auto &slot : Slots)
        if (slot.Actor) state.Records.push_back(*slot.Actor);
    return state;
}

void Actors::RestoreState(const ActorsState &state) {
    if (!state.NextId || state.Records.size() > MaximumCount)
        throw std::invalid_argument("invalid actors checkpoint");
    Actors candidate;
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
    if (record.Kind != ActorKind::Player && record.Kind != ActorKind::Resident && record.Kind != ActorKind::TrainingDummy)
        throw std::invalid_argument("unknown actor kind");
    if (record.Kind == ActorKind::Player && PlayerHandle)
        throw std::logic_error("only one controlled player is supported");
    if (LiveCount == MaximumCount) return std::nullopt;
    size_t index = 0;
    while (index < Slots.size() && Slots[index].Actor) ++index;
    if (index == Slots.size()) Slots.emplace_back();
    auto &slot = Slots[index];
    const ActorHandle handle{uint32_t(index), slot.Generation, Owner};
    slot.Actor = std::move(record);
    if (slot.Actor->Kind == ActorKind::Player) PlayerHandle = handle;
    ++LiveCount;
    Changed();
    return handle;
}

std::optional<ActorHandle> Actors::Spawn(ActorKind kind, WorldPosition position) {
    if (NextId == UINT64_MAX) throw std::overflow_error("actor identity exhausted");
    auto result = Insert({{NextId}, kind, BodyAt(position)});
    if (result) ++NextId;
    return result;
}

std::optional<ActorHandle> Actors::Restore(const ActorRecord &record) {
    if (record.Id.Value == 0 || record.Id.Value == UINT64_MAX || Find(record.Id))
        throw std::invalid_argument("invalid or duplicate persistent actor identity");
    auto normalized = record.Body.GetWorldPosition();
    normalized.Normalize();
    if (normalized.Chunk != record.Body.OriginChunk || normalized.Local != record.Body.GetWorldPosition().Local)
        throw std::invalid_argument("actor record position is not normalized");
    const auto result = Insert(record);
    if (result) NextId = std::max(NextId, record.Id.Value + 1);
    return result;
}

bool Actors::Remove(ActorHandle handle) {
    if (!Find(handle)) return false;
    auto &slot = Slots[handle.Slot];
    if (slot.Generation == UINT64_MAX) throw std::overflow_error("actor generation exhausted");
    slot.Actor.reset();
    ++slot.Generation;
    if (PlayerHandle == handle) PlayerHandle.reset();
    --LiveCount;
    Changed();
    return true;
}

void Actors::Clear() {
    for (uint32_t index = 0; index < Slots.size(); ++index)
        if (Slots[index].Actor) Remove({index, Slots[index].Generation, Owner});
}

const ActorRecord *Actors::Find(ActorHandle handle) const {
    if (handle.Owner != Owner || handle.Slot >= Slots.size()) return nullptr;
    const auto &slot = Slots[handle.Slot];
    return slot.Generation == handle.Generation && slot.Actor ? &*slot.Actor : nullptr;
}

std::optional<ActorHandle> Actors::Resolve(ActorId id) const {
    if (id.Value == 0) return std::nullopt;
    for (uint32_t index = 0; index < Slots.size(); ++index)
        if (Slots[index].Actor && Slots[index].Actor->Id == id)
            return ActorHandle{index, Slots[index].Generation, Owner};
    return std::nullopt;
}

const ActorRecord *Actors::Find(ActorId id) const {
    const auto handle = Resolve(id);
    return handle ? Find(*handle) : nullptr;
}

const ActorRecord &Actors::Require(ActorHandle handle) const {
    const auto *actor = Find(handle);
    if (!actor) throw std::logic_error("expired internal actor handle");
    return *actor;
}

const Body *Actors::Player() const { return PlayerHandle ? &Require(*PlayerHandle).Body : nullptr; }

std::optional<ActorSnapshot> Actors::PlayerSnapshot() const {
    if (!PlayerHandle) return std::nullopt;
    const auto &actor = Require(*PlayerHandle);
    return ActorSnapshot{actor.Id, actor.Kind, actor.Body.GetWorldPosition(), actor.Body.Grounded};
}

std::vector<ActorSnapshot> Actors::Snapshot() const {
    std::vector<ActorSnapshot> result;
    result.reserve(LiveCount);
    for (const auto &slot : Slots)
        if (slot.Actor) {
            const auto &actor = *slot.Actor;
            result.push_back({actor.Id, actor.Kind, actor.Body.GetWorldPosition(), actor.Body.Grounded});
        }
    return result;
}

void Actors::SetPlayerPosition(WorldPosition position) {
    auto body = BodyAt(position);
    if (PlayerHandle) {
        Slots[PlayerHandle->Slot].Actor->Body = body;
        Changed();
    } else if (!Spawn(ActorKind::Player, position)) {
        throw std::logic_error("no capacity for the session player");
    }
}

void Actors::Step(WorldMapView terrain, PlayerInput input) {
    PIXEL_PROFILE_SCOPE(Character);
    bool advanced = false;
    for (auto &slot : Slots) {
        // A training target is anchored even if its support is mined away or
        // water/terrain enters its body. It has no movement or attack behavior.
        if (!slot.Actor || slot.Actor->Kind == ActorKind::TrainingDummy || !HasBodyTerrain(slot.Actor->Body, terrain)) continue;
        auto &actor = *slot.Actor;
        const auto intent = actor.Kind == ActorKind::Player ? input : PlayerInput{};
        actor.Body.Tick(1.f / 60, terrain, intent.Horizontal, intent.Jump);
        advanced = true;
    }
    if (advanced) Changed();
}

bool Actors::Occupies(CellCoord cell) const {
    for (const auto &slot : Slots)
        if (slot.Actor && slot.Actor->Body.Occupies(cell.X, cell.Y)) return true;
    return false;
}
} // namespace pixel
