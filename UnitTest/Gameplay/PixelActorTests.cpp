#include "Runtime/Gameplay/Actors/Actors.h"
#include "Runtime/Gameplay/Actors/BodyComponent.h"
#include "Runtime/Gameplay/Actors/FormulaComponent.h"
#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Render/PixelWorldPresentation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
template<class Error, class Function> void Throws(Function function, const char *message) {
    try { function(); } catch (const Error &) { return; }
    throw std::runtime_error(message);
}

class LifetimeComponent final : public Component {
  public:
    LifetimeComponent(Actor &owner, int &destroyed) : Component(owner), Destroyed(destroyed) {}
    ~LifetimeComponent() override { ++Destroyed; }

  private:
    int &Destroyed;
};

void ComponentOwnership() {
    static_assert(!std::is_copy_constructible_v<Actor> && !std::is_move_constructible_v<Actor>);
    static_assert(!std::is_copy_constructible_v<FormulaComponent>);
    static_assert(std::is_same_v<decltype(std::declval<const Actor &>().GetComponent<FormulaComponent>()),
                                 const FormulaComponent *>);
    int destroyed = 0;
    {
        Actor actor({{1}, ActorKind::Player, {}});
        auto &lifetime = actor.AddComponent<LifetimeComponent>(destroyed);
        Require(&lifetime.GetOwner() == &actor, "component did not retain its owner");
        Require(!actor.GetComponent<FormulaComponent>(), "absent component lookup succeeded");
        auto &formula = actor.AddComponent<FormulaComponent>(api::ProgramRef{2, 7});
        Require(&formula.GetOwner() == &actor && actor.GetComponent<LifetimeComponent>() == &lifetime,
                "component allocation invalidated an existing owner or component");
        Throws<std::logic_error>([&] { actor.AddComponent<LifetimeComponent>(destroyed); },
                                 "duplicate component was accepted");
        Require(actor.ComponentCount() == 3 && destroyed == 0, "duplicate insertion changed ownership");
        const auto saved = actor.CaptureState();
        actor.EquipFormula({{3, 8}});
        Require(actor.GetComponent<FormulaComponent>() == &formula && formula.GetProgram() == api::ProgramRef{3, 8}
                    && saved.Formula->Program == api::ProgramRef{2, 7},
                "equipment update replaced the component or mutated captured state");
        Throws<std::invalid_argument>([&] { formula.Equip({}); }, "invalid program was accepted");
        Require(formula.GetProgram() == api::ProgramRef{3, 8}, "rejected equipment replaced the program");
    }
    Require(destroyed == 1, "Actor did not destroy its component exactly once");
}

void PlayerComponentLifecycle() {
    Actors actors;
    const auto handle = *actors.Spawn(ActorKind::Player, WorldPosition::FromCells(10, 10));
    const auto *player = actors.PlayerActor();
    const auto id = player->GetId();
    const auto *body = player->GetComponent<BodyComponent>();
    actors.EquipFormula(id, {{2, 7}});
    const auto *formula = player->GetComponent<FormulaComponent>();
    const auto captured = actors.Find(handle);
    for (size_t i = 1; i < Actors::MaximumCount; ++i) actors.Spawn(ActorKind::Resident, {});
    Require(actors.PlayerActor() == player && &formula->GetOwner() == player && actors.Player() == &body->GetBody(),
            "registry growth moved the player or detached its components");

    actors.SetPlayerPosition(WorldPosition::FromCells(20, 20));
    const auto launch = formula->Aim(WorldPosition::FromCells(30, 22), 17.5f, {1, 0});
    Require(launch.Center.Cell() == CellCoord{21, 22} && captured->Body.GetWorldPosition().Cell() == CellCoord{10, 10},
            "formula used stale body data or a value query borrowed live state");
    const auto state = actors.CaptureState();
    auto invalid = state;
    invalid.Records.front().Formula = FormulaEquipment{};
    const auto revision = actors.Revision();
    Throws<std::invalid_argument>([&] { actors.RestoreState(invalid); }, "invalid component state was restored");
    Require(actors.PlayerActor() == player && actors.Revision() == revision && actors.Find(handle),
            "failed component restore changed the live registry");

    actors.RestoreState(state);
    const auto *restored = actors.PlayerActor();
    Require(!actors.Find(handle) && restored->GetId() == id && restored->ComponentCount() == 2
                && &restored->GetComponent<FormulaComponent>()->GetOwner() == restored
                && restored->GetComponent<FormulaComponent>()->GetProgram() == api::ProgramRef{2, 7},
            "restore retained old handles or failed to reconstruct component ownership");
    actors.Remove(*actors.Resolve(id));
    Require(!actors.PlayerActor() && !actors.Player(), "player removal retained runtime components");
    const auto replacement = *actors.Spawn(ActorKind::Player, {});
    Require(!actors.Find(replacement)->Formula && actors.PlayerActor()->ComponentCount() == 1,
            "slot reuse inherited removed components");
    actors.Clear();
    Require(!actors.PlayerActor() && actors.Count() == 0, "clear retained component owners");
}

void UnifiedActorLifecycle() {
    for (const auto kind : {ActorKind::Player, ActorKind::Resident, ActorKind::TrainingDummy}) {
        Actors actors;
        const auto handle = *actors.Spawn(kind, WorldPosition::FromCells(20, 20));
        const auto *actor = actors.FindActor(handle);
        Require(actor && actor->GetKind() == kind, "role did not create a runtime Actor");
        const auto id = actor->GetId();
        Require(actors.FindActor(id) == actor, "runtime lookup disagrees with persistent identity");
        const auto *body = actor->GetComponent<BodyComponent>();
        const auto motion = kind == ActorKind::TrainingDummy ? BodyMotion::Anchored : BodyMotion::Dynamic;
        Require(body && &body->GetOwner() == actor && body->GetMotionMode() == motion,
                "role lost its body ownership or motion policy");
        actors.EquipFormula(id, {{2, 7}});
        const auto *formula = actor->GetComponent<FormulaComponent>();
        Require(formula && &formula->GetOwner() == actor && formula->GetProgram() == api::ProgramRef{2, 7}
                    && formula->Aim(WorldPosition::FromCells(30, 22), 17.5f, {1, 0}).Center.Cell() == CellCoord{21, 22},
                "non-player formula did not use the shared component architecture");
        for (int i = 0; i < 32; ++i) actors.Spawn(ActorKind::Resident, {});
        Require(actors.FindActor(handle) == actor && actor->GetComponent<FormulaComponent>() == formula,
                "collection growth moved an Actor or its components");

        const auto saved = actors.CaptureState();
        auto invalid = saved;
        invalid.Records.front().Formula = FormulaEquipment{};
        const auto revision = actors.Revision();
        Throws<std::invalid_argument>([&] { actors.RestoreState(invalid); }, "invalid non-player component restored");
        Require(actors.FindActor(handle) == actor && actors.Revision() == revision,
                "failed restore changed live actor ownership");
        Actors other;
        other.RestoreState(saved);
        Require(!other.FindActor(handle), "runtime Actor handle crossed registry ownership");
        actors.RestoreState(saved);
        const auto *restored = actors.FindActor(id);
        Require(!actors.FindActor(handle) && restored && restored->GetKind() == kind
                    && restored->GetComponent<BodyComponent>()->GetMotionMode() == motion
                    && &restored->GetComponent<FormulaComponent>()->GetOwner() == restored
                    && restored->GetComponent<FormulaComponent>()->GetProgram() == api::ProgramRef{2, 7},
                "restore lost role, motion policy or component state");

        const auto current = *actors.Resolve(id);
        actors.Remove(current);
        Require(!actors.FindActor(current) && !actors.FindActor(id), "deleted Actor is still reachable");
        const auto nextKind = kind == ActorKind::TrainingDummy ? ActorKind::Resident : ActorKind::TrainingDummy;
        const auto next = *actors.Spawn(nextKind, {});
        const auto *replacement = actors.FindActor(next);
        Require(next.Slot == current.Slot && next.Generation != current.Generation
                    && replacement->GetKind() == nextKind && !replacement->GetComponent<FormulaComponent>(),
                "slot reuse retained the old role or component");
        actors.Clear();
        Require(!actors.FindActor(next) && actors.Count() == 0, "clear retained a runtime Actor");
    }
}

void IdentitiesAndReuse() {
    Actors actors;
    const auto player = *actors.Spawn(ActorKind::Player, WorldPosition::FromCells(10, 10));
    const auto resident = *actors.Spawn(ActorKind::Resident, WorldPosition::FromCells(20, 10));
    const auto saved = actors.Require(resident);
    Require(saved.Id != actors.Require(player).Id && actors.Resolve(saved.Id) == resident, "actors need distinct stable identities");
    Require(actors.Remove(resident) && !actors.Remove(resident) && !actors.Find(saved.Id), "deleted actor remains reachable");
    const auto replacement = *actors.Spawn(ActorKind::Resident, WorldPosition::FromCells(30, 10));
    Require(replacement.Slot == resident.Slot && replacement.Generation != resident.Generation
                && actors.Require(replacement).Id != saved.Id && !actors.Find(resident), "slot reuse resurrected an old reference");
    Throws<std::logic_error>([&] { actors.Require(resident); }, "internal stale handle must fail early");
    const auto restored = *actors.Restore(saved);
    Require(restored.Slot != resident.Slot && actors.Require(restored).Id == saved.Id
                && pixel::CentimetersToCells(actors.Require(restored).Body.X) == pixel::CentimetersToCells(saved.Body.X) && !actors.Find(resident), "restore confused persistent ID with storage slot");
    Throws<std::invalid_argument>([&] { actors.Restore(saved); }, "duplicate restored ID accepted");
    Throws<std::logic_error>([&] { actors.Spawn(ActorKind::Player, {}); }, "duplicate player accepted");
    Actors another;
    another.Restore(saved);
    Require(!another.Find(restored) && !another.Remove(restored), "handle crossed collection lifetime");
    const auto highestId = actors.Require(replacement).Id.Value;
    actors.Clear();
    Require(!actors.Player() && !actors.Find(player) && !actors.Find(restored), "clear kept actor references alive");
    const auto next = *actors.Spawn(ActorKind::Resident, {});
    Require(actors.Require(next).Id.Value > highestId && !actors.Find(resident), "clear reused a durable ID");
    ActorRecord distantId{{9007199254740993ull}, ActorKind::Resident, {}};
    another.Restore(distantId);
    const auto afterRestore = *another.Spawn(ActorKind::Resident, {});
    Require(another.Require(afterRestore).Id.Value > distantId.Id.Value, "restore did not advance identity allocation");
}

void BoundedCollection() {
    Actors actors;
    for (size_t i = 0; i < Actors::MaximumCount; ++i)
        Require(actors.Spawn(ActorKind::Resident, {}).has_value(), "actor capacity exhausted too early");
    const auto revision = actors.Revision();
    Require(!actors.Spawn(ActorKind::Resident, {}) && actors.Count() == Actors::MaximumCount
                && actors.Revision() == revision, "capacity refusal changed state");
}

void ResidentTerrainAndRestore() {
    WorldMap map(64, 64);
    map.Fill(map.Bounds(), {CellTypeId::Air, {}});
    map.Fill({0, 40, 64, 24}, {CellTypeId::Dirt, {}});
    Actors actors;
    const auto player = *actors.Spawn(ActorKind::Player, WorldPosition::FromCells(10.125, 10));
    const auto resident = *actors.Spawn(ActorKind::Resident, WorldPosition::FromCells(30.125, 10));
    const auto frozen = *actors.Spawn(ActorKind::Resident, WorldPosition::FromCells(80, 10));
    const auto mapVersion = map.GetVersion();
    for (int step = 0; step < 120; ++step) actors.Step(map.Read(), {});
    Require(actors.Require(resident).Body.Grounded && std::abs(pixel::CentimetersToCells(actors.Require(resident).Body.Y) - 36) < 1e-6
                && actors.Require(player).Body.Grounded, "second actor did not land on read-only terrain");
    actors.Step(map.Read(), {1, true});
    Require(pixel::CentimetersToCells(actors.Require(player).Body.X) > 10.125 && pixel::CentimetersToCells(actors.Require(player).Body.Y) < 36
                && pixel::CentimetersToCells(actors.Require(resident).Body.X) == 30.125 && std::abs(pixel::CentimetersToCells(actors.Require(resident).Body.Y) - 36) < 1e-6,
            "player intent leaked into resident behavior");
    Require(pixel::CentimetersToCells(actors.Require(frozen).Body.Y) == 10 && map.GetVersion() == mapVersion, "actor wrote terrain or advanced outside active domain");

    // Copying a trusted runtime record retains falling/jumping state, while a new
    // collection assigns a fresh handle. Disk checkpoint format belongs to M4.6.
    const auto saved = actors.Require(player);
    Actors restored;
    const auto handle = *restored.Restore(saved);
    for (int step = 0; step < 12; ++step) {
        actors.Step(map.Read(), {});
        restored.Step(map.Read(), {});
    }
    Require(restored.Require(handle).Body.GetWorldPosition().Local == actors.Require(player).Body.GetWorldPosition().Local,
            "restored actor lost motion state");
    map.Fill({24, 0, 20, 40}, {CellTypeId::Water, {}});
    for (int step = 0; step < 120; ++step) actors.Step(map.Read(), {});
    Require(pixel::CentimetersToCells(actors.Require(resident).Body.Y) < 36, "resident did not respond to water buoyancy");
}

void PartialTerrainFreezes() {
    WorldMap map(128, 128);
    map.Fill(map.Bounds(), {CellTypeId::Air, {}});
    Actors actors;
    const auto resident = *actors.Spawn(ActorKind::Resident, WorldPosition::FromCells(10, 62));
    const auto revision = actors.Revision();
    actors.Step(map.View({0, 0, 64, 64}), {});
    Require(pixel::CentimetersToCells(actors.Require(resident).Body.Y) == 62 && !actors.Require(resident).Body.Grounded
                && actors.Revision() == revision, "partial window relocated resident through collision escape");
    actors.Step(map.Read(), {});
    Require(pixel::CentimetersToCells(actors.Require(resident).Body.Y) > 62, "resident did not resume after full body became active");

    WorldMap sparse;
    auto upper = std::make_unique<WorldChunk>(CellCoord{0, 0});
    upper->Initialize(std::nullopt);
    sparse.Insert(std::move(upper));
    Actors streamed;
    const auto edge = storage::ChunkEdge;
    const auto split = *streamed.Spawn(ActorKind::Resident, WorldPosition::FromCells(20, edge - 2));
    const auto window = sparse.View({0, int64_t(edge) - 64, 64, 128});
    streamed.Step(window, {});
    Require(CentimetersToCells(streamed.Require(split).Body.Y) == edge - 2 && !streamed.Require(split).Body.Grounded,
            "missing body terrain was treated as solid overlap");
    auto lower = std::make_unique<WorldChunk>(CellCoord{0, 1});
    lower->Initialize(std::nullopt);
    sparse.Insert(std::move(lower));
    streamed.Step(window, {});
    Require(CentimetersToCells(streamed.Require(split).Body.Y) > edge - 2, "resident did not resume after body terrain loaded");
}

void FarCoordinates() {
    for (const int64_t sign : {-1, 1}) {
        const CellCoord chunk{sign * (int64_t{1} << 45), -sign * (int64_t{1} << 45)};
        const CellCoord origin{chunk.X * storage::ChunkEdge, chunk.Y * storage::ChunkEdge};
        WorldMap map;
        auto data = std::make_unique<WorldChunk>(chunk);
        data->Initialize(std::nullopt);
        map.Insert(std::move(data));
        map.Fill({origin.X, origin.Y + 40, 64, 24}, {CellTypeId::Dirt, {}});
        Actors actors;
        const auto handle = *actors.Spawn(ActorKind::Player, WorldPosition::FromChunkCells(chunk, {10.125, 36}));
        actors.Step(map.View({origin.X, origin.Y, 64, 64}), {1, false});
        const auto snapshot = *actors.PlayerSnapshot();
        Require(snapshot.Position.Chunk == chunk && snapshot.Id == actors.Require(handle).Id
                    && std::abs(snapshot.Position.Local[0] - (506.25 + 800. / 60)) < .001,
                "distant actor lost fractional walking precision");
        Require(actors.Occupies({origin.X + 10, origin.Y + 38}) && !actors.Occupies({origin.X + 14, origin.Y + 38}),
                "distant actor occupancy rounded through absolute doubles");
        GameCamera camera;
        camera.SetFramebufferSize({800, 600});
        camera.Reset({origin.X, origin.Y, 64, 64}, actors.Player());
        WorldGridFrame frame;
        BuildCharacterFrame(snapshot, camera.GetCamera(), {800, 600}, frame);
        const float scale = pixel::DefaultRenderPixelsPerLogicalPixel;
        Require(frame.Character && frame.Character->Bounds == std::array<float, 4>{400 - scale, 300 - 2 * scale, 2 * scale, 4 * scale}, "distant character projection differs from camera center");
        actors.Step(map.View({origin.X, origin.Y, 64, 64}), {1, false});
        camera.FollowPlayer(actors.Player(), .1f);
        Require(camera.GetCamera().GetWorldPosition().Local[0] > snapshot.Position.Local[0] + 50, "camera did not follow distant actor");
    }
}

void FormulaEquipmentAndAim() {
    Actors actors;
    const auto handle = *actors.Spawn(ActorKind::Player, WorldPosition::FromCells(10, 10));
    const auto id = actors.Require(handle).Id;
    Require(!actors.Require(handle).Formula, "actor spawn unexpectedly installed a formula");
    actors.EquipFormula(id, {{2, 7}});
    const auto *component = actors.PlayerActor()->GetComponent<FormulaComponent>();
    Require(component && component->GetProgram() == api::ProgramRef{2, 7}, "actor lost its equipped formula");
    const auto aim = [&](double x, double y) {
        return component->Aim(WorldPosition::FromCells(x, y), 17.5f, {1, 0});
    };
    const auto right = aim(30, 12), left = aim(0, 12), up = aim(11, 0), down = aim(11, 30);
    Require(std::abs(right.Origin.RelativeCellsTo({11, 12})[0] - 1.6) < 1e-6
        && right.Direction[0] == 1 && left.Direction[0] == -1
        && up.Direction[1] == -1 && down.Direction[1] == 1, "muzzle did not follow aiming side");
    const auto near = aim(11.5, 12);
    Require(near.Origin.Local == right.Origin.Local && near.Direction[0] == -1,
        "near cursor used the body center instead of the actual launch origin");
    const auto coincident = component->Aim(right.Origin, 17.5f, {1, 0});
    Require(coincident.Direction[0] == 1 && std::isfinite(coincident.AngleDegrees),
        "cursor at muzzle produced an invalid direction");

    Body distant;
    distant.RestoreState({WorldPosition::FromChunkCells({int64_t{1} << 44, -(int64_t{1} << 44)},
        {double(storage::ChunkEdge) - 1, 10})});
    auto target = distant.GetWorldPosition();
    target.Local[0] += 1000;
    target.Local[1] -= 100;
    target.Normalize();
    const auto far = FormulaComponent::AimFromBody(distant, target, 17.5f, {1, 0});
    const auto reference = far.Origin.Cell();
    const auto from = far.Origin.RelativeTo(reference), to = target.RelativeTo(reference);
    const double dx = to[0] - from[0], dy = to[1] - from[1];
    Require(std::abs(far.Direction[0] - dx / std::hypot(dx, dy)) < 1e-6
        && std::abs(far.Direction[1] - dy / std::hypot(dx, dy)) < 1e-6,
        "distant cross-chunk aim lost the cursor direction");
    const auto state = actors.CaptureState();
    actors.RestoreState(state);
    Require(actors.Find(id)->Formula->Program == api::ProgramRef{2, 7}, "runtime restore dropped equipment");
    actors.Remove(*actors.Resolve(id));
    const auto replacement = *actors.Spawn(ActorKind::Resident, {});
    Require(!actors.Require(replacement).Formula, "slot reuse inherited another actor's formula");
}

void SessionOccupancyAndPublication() {
    GameSession session;
    auto &actors = testing::SessionAccess::Actors(session);
    const auto resident = *actors.Spawn(ActorKind::Resident, WorldPosition::FromCells(100.25, 100.5));
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {500, 500, 32, 32});
    const auto player = *view->GetPlayer();
    const auto observed = session.Observe(NativeCaller::Driver, {{500, 500}, 32, 32});
    Require(observed.Snapshot && observed.Snapshot->Player && observed.Snapshot->Player->ActorId == player.Id.Value,
            "observation omitted durable actor identity");
    const auto before = session.CurrentCommit();
    const auto blocked = session.Execute(NativeCaller::Driver, api::PlaceMaterial{{100, 101}});
    Require(blocked.Rejection == api::Reason::Blocked && session.CurrentCommit() == before, "placing material ignored resident occupancy");
    actors.Remove(resident);
    Require(session.Execute(NativeCaller::Driver, api::MineRegion{{520, 512}, {}}).Applied(), "fixture mining failed");
    Require(session.Execute(NativeCaller::Driver, api::PlaceMaterial{{100, 101}}).Applied(), "removed actor still blocks placement");
    const auto destination = WorldPosition::FromCells(513.125, 508);
    testing::SessionAccess::SetPlayerPosition(session, destination);
    testing::SessionAccess::PublishFixtureChanges(session);
    Require(view->GetPlayer()->Id == player.Id && view->GetPlayer()->Position.Chunk == destination.Chunk
                && view->GetPlayer()->Position.Local == destination.Local, "player relocation changed identity or missed its destination");
    const auto spawn = WorldPosition::FromCells(511, 508);
    Require(player.Position.Chunk == spawn.Chunk && player.Position.Local == spawn.Local,
            "player relocation mutated a published value");
    const auto oldHandle = *actors.Resolve(player.Id);
    session.Control(NativeCaller::Driver, api::ResetWorld{});
    Require(!actors.Find(oldHandle) && view->GetPlayer()->Id != player.Id, "world replacement retained actor handle");
}
} // namespace
int main() {
    try {
        ComponentOwnership();
        PlayerComponentLifecycle();
        UnifiedActorLifecycle();
        IdentitiesAndReuse();
        BoundedCollection();
        ResidentTerrainAndRestore();
        PartialTerrainFreezes();
        FarCoordinates();
        FormulaEquipmentAndAim();
        SessionOccupancyAndPublication();
        std::cout << "Actors identity, lifecycle, terrain and publication passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
