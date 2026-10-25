#include "Runtime/Gameplay/Actors/Actors.h"
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

void IdentitiesAndReuse() {
    Actors actors;
    const auto player = *actors.Spawn(ActorKind::Player, WorldPosition::FromAbsolute(10, 10));
    const auto resident = *actors.Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(20, 10));
    const auto saved = actors.Require(resident);
    Require(saved.Id != actors.Require(player).Id && actors.Resolve(saved.Id) == resident, "actors need distinct stable identities");
    Require(actors.Remove(resident) && !actors.Remove(resident) && !actors.Find(saved.Id), "deleted actor remains reachable");
    const auto replacement = *actors.Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(30, 10));
    Require(replacement.Slot == resident.Slot && replacement.Generation != resident.Generation
                && actors.Require(replacement).Id != saved.Id && !actors.Find(resident), "slot reuse resurrected an old reference");
    Throws<std::logic_error>([&] { actors.Require(resident); }, "internal stale handle must fail early");
    const auto restored = *actors.Restore(saved);
    Require(restored.Slot != resident.Slot && actors.Require(restored).Id == saved.Id
                && actors.Require(restored).Body.X == saved.Body.X && !actors.Find(resident), "restore confused persistent ID with storage slot");
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
    const auto player = *actors.Spawn(ActorKind::Player, WorldPosition::FromAbsolute(10.125, 10));
    const auto resident = *actors.Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(30.125, 10));
    const auto frozen = *actors.Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(80, 10));
    const auto mapVersion = map.GetVersion();
    for (int step = 0; step < 120; ++step) actors.Step(map.Read(), {});
    Require(actors.Require(resident).Body.Grounded && std::abs(actors.Require(resident).Body.Y - 36) < 1e-6
                && actors.Require(player).Body.Grounded, "second actor did not land on read-only terrain");
    actors.Step(map.Read(), {1, true});
    Require(actors.Require(player).Body.X > 10.125 && actors.Require(player).Body.Y < 36
                && actors.Require(resident).Body.X == 30.125 && std::abs(actors.Require(resident).Body.Y - 36) < 1e-6,
            "player intent leaked into resident behavior");
    Require(actors.Require(frozen).Body.Y == 10 && map.GetVersion() == mapVersion, "actor wrote terrain or advanced outside active domain");

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
    Require(actors.Require(resident).Body.Y < 36, "resident did not respond to water buoyancy");
}

void PartialTerrainFreezes() {
    WorldMap map(128, 128);
    map.Fill(map.Bounds(), {CellTypeId::Air, {}});
    Actors actors;
    const auto resident = *actors.Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(10, 62));
    const auto revision = actors.Revision();
    actors.Step(map.View({0, 0, 64, 64}), {});
    Require(actors.Require(resident).Body.Y == 62 && !actors.Require(resident).Body.Grounded
                && actors.Revision() == revision, "partial window relocated resident through collision escape");
    actors.Step(map.Read(), {});
    Require(actors.Require(resident).Body.Y > 62, "resident did not resume after full body became active");

    WorldMap sparse;
    auto upper = std::make_unique<WorldChunk>(CellCoord{0, 0});
    upper->Initialize(std::nullopt);
    sparse.Insert(std::move(upper));
    Actors streamed;
    const auto edge = storage::ChunkEdge;
    const auto split = *streamed.Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(20, edge - 2));
    const auto window = sparse.View({0, int64_t(edge) - 64, 64, 128});
    streamed.Step(window, {});
    Require(streamed.Require(split).Body.Y == edge - 2 && !streamed.Require(split).Body.Grounded,
            "missing body terrain was treated as solid overlap");
    auto lower = std::make_unique<WorldChunk>(CellCoord{0, 1});
    lower->Initialize(std::nullopt);
    sparse.Insert(std::move(lower));
    streamed.Step(window, {});
    Require(streamed.Require(split).Body.Y > edge - 2, "resident did not resume after body terrain loaded");
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
        const auto handle = *actors.Spawn(ActorKind::Player, {chunk, {10.125, 36}});
        actors.Step(map.View({origin.X, origin.Y, 64, 64}), {1, false});
        const auto snapshot = *actors.PlayerSnapshot();
        Require(snapshot.Position.Chunk == chunk && snapshot.Id == actors.Require(handle).Id
                    && std::abs(snapshot.Position.Local[0] - (10.125 + 16. / 60)) < 1e-7,
                "distant actor lost fractional walking precision");
        Require(actors.Occupies({origin.X + 10, origin.Y + 38}) && !actors.Occupies({origin.X + 14, origin.Y + 38}),
                "distant actor occupancy rounded through absolute doubles");
        GameCamera camera;
        camera.SetFramebufferSize({800, 600});
        camera.Reset({origin.X, origin.Y, 64, 64}, actors.Player());
        WorldGridFrame frame;
        BuildCharacterFrame(snapshot, camera.GetCamera(), {800, 600}, frame);
        Require(frame.Character && frame.Character->Bounds == std::array<float, 4>{384, 268, 32, 64}, "distant character projection differs from camera center");
        actors.Step(map.View({origin.X, origin.Y, 64, 64}), {1, false});
        camera.FollowPlayer(actors.Player(), .1f);
        Require(camera.GetCamera().GetWorldPosition().Local[0] > snapshot.Position.Local[0] + 1, "camera did not follow distant actor");
    }
}

void SessionOccupancyAndPublication() {
    GameSession session;
    auto &actors = testing::SessionAccess::Actors(session);
    const auto resident = *actors.Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(100.25, 100.5));
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
    const auto destination = WorldPosition::FromAbsolute(513.125, 508);
    testing::SessionAccess::SetPlayerPosition(session, destination);
    testing::SessionAccess::PublishFixtureChanges(session);
    Require(view->GetPlayer()->Id == player.Id && view->GetPlayer()->Position.Chunk == destination.Chunk
                && view->GetPlayer()->Position.Local == destination.Local, "player relocation changed identity or missed its destination");
    const auto spawn = WorldPosition::FromAbsolute(511, 508);
    Require(player.Position.Chunk == spawn.Chunk && player.Position.Local == spawn.Local,
            "player relocation mutated a published value");
    const auto oldHandle = *actors.Resolve(player.Id);
    session.Control(NativeCaller::Driver, api::ResetWorld{});
    Require(!actors.Find(oldHandle) && view->GetPlayer()->Id != player.Id, "world replacement retained actor handle");
}
} // namespace
int main() {
    try {
        IdentitiesAndReuse();
        BoundedCollection();
        ResidentTerrainAndRestore();
        PartialTerrainFreezes();
        FarCoordinates();
        SessionOccupancyAndPublication();
        std::cout << "Actors identity, lifecycle, terrain and publication passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
