#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/Actors/MonsterComponent.h"
#include "Runtime/Gameplay/Actors/BodyComponent.h"
#include "Runtime/Gameplay/Persistence/GameplaySaveData.h"
#include "Runtime/Render/MonsterFrame.h"
#include "Runtime/Render/GamePresentation.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>

namespace {
using namespace pixel;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F action) {
    try { action(); } catch (const std::exception &) { return; }
    throw std::runtime_error("invalid monster input accepted");
}
std::vector<char> Read(std::string_view name) {
    std::ifstream file(std::filesystem::path(PIXEL_TEST_CONTENT) / name, std::ios::binary);
    if (!file) throw std::runtime_error("missing monster fixture: " + std::string(name));
    return {std::istreambuf_iterator<char>(file), {}};
}
WorldPosition At(double x, double y) { return WorldPosition::FromCells(x, y); }
ActorId Spawn(Actors &actors, std::string_view id, double x, double floor = 60) {
    auto catalog = MonsterCatalog::Parse(Read("Monsters.json"));
    return actors.Require(*actors.SpawnMonster(id, At(x, floor - catalog->Require(id).BodySizeCells[1]))).Id;
}
void AssetsAndSchema() {
    auto catalog = MonsterCatalog::Parse(Read("Monsters.json"));
    Check(catalog->Definitions.size() == 8 && catalog->Spawns.size() == 8, "missing configured monsters");
    ArtAssetStore store(Read);
    const auto art = store.LoadMonsters();
    Check(store.LoadMonsters("./Monsters.json") == art && store.CachedImageCount() == 54, "monster cache lost sheets or reread manifest");
    size_t frames = 0;
    for (const auto &d : catalog->Definitions) {
        const auto &v = art->Require(d.Id);
        for (const auto &[name, clip] : v.Clips) frames += clip.Frames.size();
        if (d.WakeTicks) Check(v.States.contains("Dormant") && v.States.contains("Wake"), "ambush visuals missing");
    }
    Check(frames == 370, "atlas did not preserve every supplied animation frame");
    const auto original = nlohmann::json::parse(Read("Monsters.json"));
    const auto reject = [&](auto edit) {
        auto json = original; edit(json);
        const auto text = json.dump();
        Reject([&] { MonsterCatalog::Parse({text.begin(), text.end()}); });
    };
    reject([](auto &j) { j["Monsters"][0]["Gameplay"]["AttackWindupTicks"] = .5; });
    reject([](auto &j) { j["Monsters"][0]["Gameplay"]["MaxHealth"] = -1; });
    reject([](auto &j) { j["Monsters"][1]["Id"] = "mushroom"; });
    reject([](auto &j) { j["Spawns"][0]["Id"] = "unknown"; });
    auto bad = original;
    bad["Monsters"][0]["Visual"]["Clips"]["idle"]["Path"] = "../RawData/Monsters/Stills/mushroom.png";
    std::string replacement = bad.dump();
    ArtAssetStore retry([&](std::string_view path) { return path == "retry.json" ? std::vector<char>(replacement.begin(), replacement.end()) : Read(path); });
    Reject([&] { retry.LoadMonsters("retry.json"); });
    replacement = original.dump();
    Check(retry.LoadMonsters("retry.json")->Monsters.size() == 8, "corrected manifest could not be retried");
}

void MotionCombatAndFreeze() {
    auto catalog = MonsterCatalog::Parse(Read("Monsters.json"));
    World world(128, 96);
    world.FillRegion({0, 60, 128, 36}, {CellTypeId::Stone});
    Actors actors(catalog);
    const auto player = actors.Require(*actors.Spawn(ActorKind::Player, At(30, 56))).Id;
    const auto mushroom = Spawn(actors, "mushroom", 26.5);
    auto step = [&](int count) { for (int i = 0; i < count; ++i) actors.Step(world.ReadState(), {}); };
    step(1);
    Check(actors.Find(mushroom)->Monster->Action == MonsterAction::Attack, "nearby monster did not wind up");
    const auto windup = catalog->Require("mushroom").AttackWindupTicks;
    step(int(windup) - 1);
    Check(actors.Find(player)->Health->Current == 100, "monster damaged player before hit tick");
    step(1);
    Check(actors.Find(player)->Health->Current == 92, "attack did not apply configured damage exactly once");
    step(2);
    Check(actors.Find(player)->Health->Current == 92, "one swing dealt damage on multiple ticks");
    const auto old = actors.Find(mushroom)->Monster->ActionTicks;
    actors.Step(world.GetMap().View({70, 0, 32, 32}), {});
    Check(actors.Find(mushroom)->Monster->ActionTicks == old, "missing terrain advanced monster state");

    const auto hp = actors.Find(mushroom)->Health->Current;
    Check(actors.Damage(mushroom, 9) && actors.Find(mushroom)->Health->Current == hp - 9
        && actors.Find(mushroom)->Monster->Action == MonsterAction::Hurt, "hit did not interrupt monster attack");
    const auto state = actors.CaptureState();
    Actors restored(catalog); restored.RestoreState(state);
    Check(restored.Find(mushroom)->Monster->ActionTicks == actors.Find(mushroom)->Monster->ActionTicks
        && restored.Find(mushroom)->Health->Current == hp - 9, "restore lost damage or animation state");
    const auto before = restored.CaptureState();
    auto invalid = before; invalid.Records.back().Monster->Definition = "missing";
    Reject([&] { restored.RestoreState(invalid); });
    Check(restored.Find(mushroom)->Health->Current == hp - 9, "invalid restore changed live actor state");
    auto protectedState = before;
    protectedState.Records.back().Health->InvulnerableTicks = 2;
    restored.RestoreState(protectedState);
    restored.Step(world.ReadState(), {});
    Check(!restored.Damage(mushroom, 1), "restored protection expired too early");
    restored.Step(world.ReadState(), {});
    Check(restored.Damage(mushroom, 1), "monster protection timer never advanced");

    Check(actors.Damage(mushroom, 1e6f), "lethal damage refused");
    Check(actors.Find(mushroom)->Monster->Action == MonsterAction::Death, "death state missing");
    Check(!actors.Damage(mushroom, 1), "corpse accepted another hit");
    step(int(catalog->Require("mushroom").DeathTicks) - 1);
    Check(bool(actors.Find(mushroom)), "corpse removed before death animation ended");
    step(1); Check(!actors.Find(mushroom), "expired corpse was retained");
    step(120);
    Check(actors.Damage(player, 1000) && actors.Find(player)->Health->Current == 100
        && actors.Player()->GetWorldPosition().Cell() == CellCoord{30, 56}, "player did not respawn after lethal damage");
}

void DodgeWallsFlightAndAmbush() {
    auto catalog = MonsterCatalog::Parse(Read("Monsters.json"));
    World world(128, 96); world.FillRegion({0, 60, 128, 36}, {CellTypeId::Stone});
    Actors actors(catalog);
    auto player = actors.Require(*actors.Spawn(ActorKind::Player, At(30, 56))).Id;
    const auto mushroom = Spawn(actors, "mushroom", 26.5);
    actors.Step(world.ReadState(), {});
    actors.SetPlayerPosition(At(55, 56));
    for (uint32_t i = 0; i < catalog->Require("mushroom").AttackDurationTicks; ++i) actors.Step(world.ReadState(), {});
    Check(actors.Find(player)->Health->Current == 100, "dodged attack still hit its old target location");
    actors.SetPlayerPosition(At(30, 56));
    world.FillRegion({29, 48, 1, 12}, {CellTypeId::Stone});
    for (int i = 0; i < 120; ++i) actors.Step(world.ReadState(), {});
    Check(actors.Find(player)->Health->Current == 100, "monster attacked through solid wall");

    Actors flyers(catalog);
    const auto bat = Spawn(flyers, "bat", 70, 35);
    for (int i = 0; i < 180; ++i) flyers.Step(world.ReadState(), {});
    Check(std::abs(flyers.Find(bat)->Body.GetWorldPosition().RelativeCellsTo({0,0})[1] - 33) < .01,
        "flying monster fell under gravity");
    world.FillRegion({75, 0, 2, 60}, {CellTypeId::Stone});
    for (int i = 0; i < 180; ++i) flyers.Step(world.ReadState(), {});
    Check(flyers.Find(bat)->Body.GetWorldPosition().RelativeCellsTo({0,0})[0] + 3 <= 75.01,
        "flying monster crossed solid terrain");

    Actors ambush(catalog);
    ambush.Spawn(ActorKind::Player, At(40, 56));
    const auto mimic = Spawn(ambush, "mimic", 20);
    for (int i = 0; i < 90; ++i) ambush.Step(world.ReadState(), {});
    Check(ambush.Find(mimic)->Monster->Action == MonsterAction::Dormant, "mimic woke outside detection range");
    ambush.SetPlayerPosition(At(25, 56)); ambush.Step(world.ReadState(), {});
    Check(ambush.Find(mimic)->Monster->Action == MonsterAction::Wake, "nearby player did not wake mimic");
}

void CornerOcclusionCombat() {
    const auto catalog = MonsterCatalog::Parse(Read("Monsters.json"));
    const auto &definition = catalog->Require("mushroom");
    for (const bool mirrorX : {false, true}) {
        for (const bool mirrorY : {false, true}) {
            for (const bool blocked : {false, true}) {
                World world(64, 64);
                if (blocked) world.FillRegion({mirrorX ? 43 : 20, mirrorY ? 43 : 20, 1, 1}, {CellTypeId::Stone});
                const auto position = [&](double x, double y, double width, double height) {
                    return At(mirrorX ? 64 - x - width : x, mirrorY ? 64 - y - height : y);
                };
                Actors actors(catalog);
                const auto player = actors.Require(*actors.Spawn(ActorKind::Player, position(20.5, 15.6, 2, 4))).Id;
                const auto monster = actors.Require(*actors.SpawnMonster("mushroom", position(17.4, 20.75, 2.2, 3.5))).Id;
                auto saved = actors.CaptureState();
                auto &attack = *saved.Records.back().Monster;
                attack.Action = MonsterAction::Attack;
                attack.ActionTicks = definition.AttackWindupTicks - 1;
                attack.CooldownTicks = definition.AttackCooldownTicks - attack.ActionTicks;
                attack.FacingLeft = mirrorX;
                actors.RestoreState(saved);
                Check(actors.Find(player)->Body.CanOccupy(world.ReadState()) && actors.Find(monster)->Body.CanOccupy(world.ReadState()),
                    "corner occlusion fixture embedded an actor in stone");

                // The centre line crosses a short interior segment of the
                // stone cell between the old quarter-cell sample positions.
                actors.Step(world.ReadState(), {});
                Check(actors.Find(player)->Health->Current == (blocked ? 100 : 100 - definition.Damage),
                    "diagonal attack did not respect stone corner occlusion");
            }
        }
    }
}

void SightLineBoundaries() {
    auto catalog = std::make_shared<MonsterCatalog>(*MonsterCatalog::Parse(Read("Monsters.json")));
    auto &definition = *std::find_if(catalog->Definitions.begin(), catalog->Definitions.end(), [](const auto &d) { return d.Id == "bat"; });
    definition.BodySizeCells = {.5f, .5f};
    definition.AttackRangeCells = 8;
    catalog->Validate();
    const auto strike = [&](std::array<double, 2> from, std::array<double, 2> to, bool stone, CellCoord obstacle = {20, 20}) {
        World world(64, 64);
        if (stone) world.FillRegion({obstacle.X, obstacle.Y, 1, 1}, {CellTypeId::Stone});
        const auto origin = At(from[0] - .25, from[1] - .25);
        Body body; body.RestoreState({origin});
        ActorRecord record{{1}, ActorKind::Monster, body};
        record.Monster = MonsterState{"bat", origin, MonsterAction::Attack, definition.AttackWindupTicks - 1,
            definition.AttackCooldownTicks, to[0] < from[0]};
        record.Health = HealthState{definition.MaxHealth, definition.MaxHealth, 0, origin};
        Actor actor(record, catalog);
        Body target; target.RestoreState({At(to[0] - 1, to[1] - 2)});
        Check(actor.GetComponent<BodyComponent>()->GetBody().CanOccupy(world.ReadState()) && target.CanOccupy(world.ReadState()),
            "sight-line fixture embedded a body in stone");
        return actor.GetComponent<MonsterComponent>()->Step(world.ReadState(), target).Strike;
    };
    Check(!strike({18.5, 20.5}, {22.5, 20.5}, true), "horizontal sight crossed stone");
    Check(!strike({22.5, 20.5}, {18.5, 20.5}, true), "reverse sight crossed stone");
    Check(!strike({20.5, 18.5}, {20.5, 23.5}, true), "vertical sight crossed stone");
    Check(!strike({18.5, 21.5}, {22.5, 17.5}, true), "exact corner contact did not block sight");
    Check(strike({18.5, 21.25}, {22.5, 17.25}, true), "clear near-corner sight was blocked");
    Check(strike({18.5, 18.5}, {18.5, 18.5}, false), "zero-length clear sight was blocked");
    for (const auto endpoint : {std::array{18., 19.}, std::array{19., 18.}, std::array{18., 18.}, std::array{19., 19.}})
        Check(strike({18.5, 18.5}, endpoint, false), "grid-corner endpoint was skipped or overshot");
    // A 1:7 line crosses a vertex after several horizontal boundaries. Adding
    // rounded crossing times used to miss its touching side cell.
    Check(!strike({5.5, 5.5}, {6.5, 12.5}, true, {6, 8}), "steep grid corner did not block sight");
    Check(!strike({6.5, 12.5}, {5.5, 5.5}, true, {6, 8}), "reverse steep grid corner did not block sight");
    Check(!strike({5.5, 5.5}, {12.5, 6.5}, true, {8, 6}), "shallow grid corner did not block sight");
    Check(!strike({12.5, 6.5}, {5.5, 5.5}, true, {8, 6}), "reverse shallow grid corner did not block sight");
    Check(strike({5.375, 5.5}, {6.375, 12.5}, true, {6, 8}), "clear steep near-corner sight was blocked");
    Check(strike({5.5, 5.375}, {12.5, 6.375}, true, {8, 6}), "clear shallow near-corner sight was blocked");
}

void ProjectileDamageAndCheckpoint() {
    auto catalog = MonsterCatalog::Parse(Read("Monsters.json"));
    World world(64, 64); Actors actors(catalog);
    const auto target = Spawn(actors, "rat", 20, 30);
    const auto targetPosition = actors.Find(target)->Body.GetWorldPosition();
    auto artifact = formula::process::Compile("monster-hit", "formula 2; run bolt(10, speed(600), ticks(30));");
    Check(std::holds_alternative<formula::process::ProgramArtifact>(artifact), "bolt compile failed");
    FormulaBridge bridge; bridge.BindActors(actors);
    bridge.ApplyProcess(std::get<formula::process::ProgramArtifact>(artifact));
    ProcessCarrier carrier; carrier.Kind = ProcessCarrierKind::Player; carrier.Subject = 1; carrier.Scope = {0, 0, 64, 64};
    carrier.Origin = FormulaWorldAdapter::OffsetCells(targetPosition, {-8, .9});
    bridge.BeginFrame(); Check(bridge.CastCarrier(world, carrier), "monster cast refused");
    for (int i = 0; i < 4; ++i) { bridge.BeginFrame(); bridge.Step(world); }
    Check(actors.Find(target)->Health->Current == 15, "swept projectile did not damage monster");
    carrier.Origin = FormulaWorldAdapter::OffsetCells(targetPosition, {1, .9});
    bridge.BeginFrame(); Check(bridge.CastCarrier(world, carrier), "overlap cast refused");
    for (int i = 0; i < 4; ++i) { bridge.BeginFrame(); bridge.Step(world); }
    Check(actors.Find(target)->Health->Current == 5, "birth overlap did not damage monster");
    GameplaySaveData save; save.PlayerBag = items::ContainerId{1}; save.Programs = "{}";
    save.Actors = actors.CaptureState();
    const auto encoded = EncodeGameplayState(save);
    Check(encoded.starts_with("PixelGameplay 6\n"), "combat checkpoint format was not versioned");
    auto decoded = DecodeGameplayState(encoded);
    Actors restored(catalog); restored.RestoreState(decoded.Actors);
    Check(restored.Find(target)->Monster->Definition == "rat" && restored.Find(target)->Health->Current == 5
        && restored.Find(target)->Body.Size == actors.Find(target)->Body.Size, "checkpoint lost monster identity, size or health");
}

void PersistentWorld() {
    io::TemporaryDirectory files("pixel-monster-save");
    struct SaveEnvironment {
        std::optional<std::string> Previous;
        explicit SaveEnvironment(const std::filesystem::path &path) {
            if (const auto *old = std::getenv("PIXEL_SAVE_ROOT")) Previous = old;
            Set(path.string().c_str());
        }
        static void Set(const char *value) {
#if defined(_WIN32)
            _putenv_s("PIXEL_SAVE_ROOT", value ? value : "");
#else
            if (value) setenv("PIXEL_SAVE_ROOT", value, 1); else unsetenv("PIXEL_SAVE_ROOT");
#endif
        }
        ~SaveEnvironment() { Set(Previous ? Previous->c_str() : nullptr); }
    } environment(files.Path());
    const auto catalog = MonsterCatalog::Parse(Read("Monsters.json"));
    WorldGenerationSettings generation; generation.Kind = WorldGeneratorKind::Flat;
    ActorId id;
    {
        GameSession session(SessionMode::Persistent, generation, catalog);
        Check(session.GetActors().Count() == 10, "persistent new world omitted monsters");
        const auto values = session.GetActors().Snapshot();
        id = std::find_if(values.begin(), values.end(), [](const auto &actor) { return actor.Monster.has_value(); })->Id;
        testing::SessionAccess::Actors(session).Damage(id, 7);
        testing::SessionAccess::CommitFixtureChanges(session);
        Check(session.Control(NativeCaller::Driver, api::SaveWorld{}).Succeeded(), "monster checkpoint was not saved");
    }
    GameSession restored(SessionMode::Persistent, generation, catalog);
    const auto actor = restored.GetActors().Find(id);
    Check(restored.GetActors().Count() == 10 && actor && actor->Monster->Definition == "mushroom"
        && actor->Health->Current == 33 && actor->Monster->Action == MonsterAction::Hurt,
        "disk restore lost monster state or spawned duplicate monsters");
}

void SpawnAndPresentation() {
    auto catalog = MonsterCatalog::Parse(Read("Monsters.json"));
    ArtAssetStore store(Read); const auto art = store.LoadMonsters();
    GameSession session(SessionMode::Finite, {}, catalog);
    Check(session.GetActors().Count() == 10, "configured new world did not spawn eight monsters");
    GameCamera camera; camera.SetFramebufferSize({1280, 720});
    camera.Reset(session.GetWorld().GetSimulationBounds(), session.GetPlayer());
    camera.SetSceneScale(5.f / DefaultRenderPixelsPerLogicalPixel);
    GamePresentation presentation;
    auto publish = presentation.CreateFramePublisher(session, camera, {}, nullptr, art);
    WorldGridFrame frame; publish(frame);
    Check(frame.MonsterDraws.size() == 8, "committed monster collection did not reach presentation");
    const auto first = frame.MonsterDraws.front().FrameTexels;
    publish(frame); Check(frame.MonsterDraws.front().FrameTexels == first, "repeated draw advanced animation");
    presentation.ToggleFieldView(); publish(frame);
    Check(frame.MonsterDraws.empty() && frame.ActorDraws.empty(), "monster leaked into F1 diagnostic view");
    presentation.ToggleFieldView();
    Check(session.Control(NativeCaller::Driver, api::SetPaused{true}).Succeeded(), "pause refused");
    session.Tick(.1f, {}); publish(frame);
    Check(frame.MonsterDraws.front().FrameTexels == first, "pause advanced monster animation");
    auto snapshots = session.GetActors().Snapshot();
    auto &monster = *std::find_if(snapshots.begin(), snapshots.end(), [](const auto &a) { return a.Monster.has_value(); });
    monster.Monster->Action = MonsterAction::Move; monster.Monster->ActionTicks = 6;
    frame.ActorDraws.clear(); BuildMonsterFrame(snapshots, *art, camera.GetCamera(), {1280,720}, frame);
    const auto forward = frame.MonsterDraws.front();
    monster.Monster->FacingLeft = true;
    frame.ActorDraws.clear(); BuildMonsterFrame(snapshots, *art, camera.GetCamera(), {1280,720}, frame);
    Check(frame.MonsterDraws.front().FacingLeft && frame.MonsterDraws.front().FrameTexels == forward.FrameTexels,
        "facing change selected a different animation frame");
    Check(session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded() && session.GetActors().Count() == 10,
        "new-world reset duplicated or lost monsters");
}
}
int main() {
    try {
        AssetsAndSchema(); MotionCombatAndFreeze(); DodgeWallsFlightAndAmbush(); CornerOcclusionCombat(); SightLineBoundaries();
        ProjectileDamageAndCheckpoint(); PersistentWorld(); SpawnAndPresentation();
        std::cout << "Monster assets, AI, combat, projectiles, checkpoint and presentation passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
