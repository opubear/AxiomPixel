#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/Persistence/GameplaySaveData.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>

namespace {
using namespace pixel;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F action) {
    try { action(); } catch (const std::exception &) { return; }
    throw std::runtime_error("invalid population checkpoint accepted");
}
struct Environment {
    std::string Name;
    std::optional<std::string> Previous;
    static void Set(const std::string &name, const char *value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value ? value : "");
#else
        if (value) setenv(name.c_str(), value, 1); else unsetenv(name.c_str());
#endif
    }
    Environment(std::string name, const std::string &value) : Name(std::move(name)) {
        if (const auto *old = std::getenv(Name.c_str())) Previous = old;
        Set(Name, value.c_str());
    }
    ~Environment() { Set(Name, Previous ? Previous->c_str() : nullptr); }
};
std::shared_ptr<const MonsterCatalog> Catalog() {
    std::ifstream file(std::filesystem::path(PIXEL_TEST_CONTENT) / "Monsters.json");
    Check(bool(file), "missing population content");
    return MonsterCatalog::Parse({std::istreambuf_iterator<char>(file), {}});
}
WorldGenerationSettings Natural() {
    WorldGenerationSettings settings{WorldGeneratorKind::Natural, 2, 42};
    settings.WaterEnabled = false;
    return settings;
}
void Move(GameSession &session, int64_t x) {
    auto &world = testing::SessionAccess::World(session);
    const auto surface = CreateWorldGenerator(world.GetGenerationSettings())->SurfaceHeight(x);
    testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromCell({x, surface - 4}));
    world.LoadSimulationNow();
}
void Step(GameSession &session) {
    const auto tick = session.CurrentCommit().Tick;
    Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "single step rejected");
    session.Tick(1.f / 60, {});
    Check(session.CurrentCommit().Tick == tick + 1, "population step did not commit");
}
size_t LocalPopulation(const GameSession &session) {
    const auto bounds = session.GetWorld().GetSimulationBounds();
    const auto actors = session.GetActors().Snapshot();
    return std::count_if(actors.begin(), actors.end(), [&](const auto &actor) {
        return (actor.Kind == ActorKind::Monster || actor.Kind == ActorKind::Resident) && bounds.Contains(actor.Position.Cell());
    });
}
void ExplorationAndCheckpoint() {
    io::TemporaryDirectory files("pixel-biome-population-save");
    Environment saveRoot("PIXEL_SAVE_ROOT", files.Path().string()), window("PIXEL_SIMULATION_WINDOW", "512");
    const auto catalog = Catalog();
    ActorId wounded, dead;
    ActorRecord beforeSleep;
    std::vector<PopulationDecision> savedDecisions;
    {
        GameSession session(SessionMode::Persistent, Natural(), catalog);
        session.Control(NativeCaller::Driver, api::SetPaused{true});
        const auto before = testing::SessionAccess::Population(session).CaptureState().Decisions.size();
        Move(session, 9088);
        auto view = session.CreateReadView();
        session.RefreshReadView(*view, session.GetWorld().GetSimulationBounds());
        session.Tick(.1f, {});
        Check(!LocalPopulation(session) && testing::SessionAccess::Population(session).CaptureState().Decisions.size() == before,
            "camera residency or paused tick generated population");
        for (const auto x : {int64_t{9088}, int64_t{4096}, int64_t{15360}}) {
            Move(session, x);
            Step(session);
            Check(LocalPopulation(session) > 0, "exploration reached an empty forest/desert/snow region");
            Check(!testing::SessionAccess::Population(session).CaptureState().Dormant.empty(), "departed population retained active slots");
        }
        Move(session, 9088); Step(session);
        auto &actors = testing::SessionAccess::Actors(session);
        std::vector<ActorId> localMonsters;
        for (const auto &actor : actors.Snapshot())
            if (actor.Monster && session.GetWorld().GetSimulationBounds().Contains(actor.Position.Cell())) localMonsters.push_back(actor.Id);
        Check(localMonsters.size() >= 2, "forest fixture needs two monsters");
        wounded = localMonsters[0]; dead = localMonsters[1];
        actors.Damage(wounded, 7);
        actors.Damage(dead, 1000000);
        auto dying = *actors.Find(dead);
        dying.Monster->ActionTicks = catalog->Require(dying.Monster->Definition).DeathTicks - 1;
        actors.Remove(*actors.Resolve(dead)); actors.Restore(dying);
        Step(session);
        Check(!actors.Find(dead), "monster death did not retire identity");
        beforeSleep = *actors.Find(wounded);
        Move(session, 4096); Step(session);
        Check(!actors.Find(wounded), "far ecological actor was not made dormant");
        savedDecisions = testing::SessionAccess::Population(session).CaptureState().Decisions;
        const auto saved = session.Control(NativeCaller::Driver, api::SaveWorld{});
        Check(saved.Succeeded(), "population checkpoint failed");
    }
    // Reordering the content list must not reinterpret saved candidate identity.
    auto reordered = std::make_shared<MonsterCatalog>(*catalog);
    std::reverse(reordered->Spawns.begin(), reordered->Spawns.end());
    GameSession restored(SessionMode::Persistent, Natural(), reordered);
    Move(restored, 9088); Step(restored);
    const auto after = restored.GetActors().Find(wounded);
    Check(after && after->Health->Current == beforeSleep.Health->Current
        && after->Monster->ActionTicks == beforeSleep.Monster->ActionTicks
        && after->Body.GetWorldPosition().Chunk == beforeSleep.Body.GetWorldPosition().Chunk
        && after->Body.GetWorldPosition().Local == beforeSleep.Body.GetWorldPosition().Local,
        "dormant restore lost identity, health, behavior or position");
    Check(!restored.GetActors().Find(dead), "revisited region resurrected killed monster");
    for (const auto &old : savedDecisions) {
        const auto current = testing::SessionAccess::Population(restored).CaptureState().Decisions;
        const auto found = std::find_if(current.begin(), current.end(), [&](const auto &entry) { return entry.Site == old.Site; });
        Check(found != current.end() && found->Actor == old.Actor, "save/load or catalog reorder replaced a candidate identity");
    }
    std::cout << "Three biomes, pause, death, dormancy and disk restore passed\n";
}

struct PopulationFixture {
    io::TemporaryDirectory Files{"pixel-population-fixture"};
    World WorldValue;
    std::shared_ptr<MonsterCatalog> CatalogValue = std::make_shared<MonsterCatalog>();
    Actors Characters{CatalogValue};
    RegionalPopulation Population;

    // Build the catalog before Actors validates it.
    static std::shared_ptr<MonsterCatalog> FixtureCatalog() {
        auto catalog = std::make_shared<MonsterCatalog>();
        MonsterDefinition definition; definition.Id = "test"; definition.Damage = 0;
        catalog->Definitions.push_back(definition);
        for (const int offset : {-80, -60, -40, -20, 20, 40, 60, 80}) catalog->Spawns.push_back({"test", offset, 0});
        return catalog;
    }
    PopulationFixture() : CatalogValue(FixtureCatalog()), Characters(CatalogValue) {
        WorldStreamSettings stream; stream.Directory = Files.Path(); stream.Generation = Natural(); stream.WindowEdge = 256;
        WorldValue.EnableStreaming(stream, {384, 384, 256, 256});
        MoveTo(511);
    }
    void MoveTo(int64_t x) {
        const auto surface = CreateWorldGenerator(Natural())->SurfaceHeight(x);
        const auto position = WorldPosition::FromCell({x, surface - 4});
        Characters.SetPlayerPosition(position);
        WorldValue.SetSimulationFocus(position); WorldValue.UpdateStreaming(); WorldValue.LoadSimulationNow();
    }
    void Activate(std::span<const ActorId> pinned = {}) { Population.Activate(WorldValue, Characters, CatalogValue.get(), pinned, false); }
};

void CapacityDormancyAndValidation() {
    PopulationFixture fixture;
    fixture.Activate();
    const auto first = fixture.Population.CaptureState().Decisions;
    const auto found = std::find_if(first.begin(), first.end(), [](const auto &entry) { return entry.Actor.Value != 0; });
    Check(found != first.end(), "capacity fixture has no initial actor");
    const auto original = found->Actor;
    fixture.Characters.Damage(original, 7);
    const auto injured = *fixture.Characters.Find(original);
    size_t total = 0;
    for (int region = 1; region <= 80 && total <= Actors::MaximumCount + 16; ++region) {
        fixture.MoveTo(region * 1024 + 511);
        fixture.Activate();
        const auto state = fixture.Population.CaptureState();
        total = state.Dormant.size() + fixture.Characters.Count();
        Check(fixture.Characters.Count() < 32, "exploration leaked active actor slots");
    }
    Check(total > Actors::MaximumCount, "fixture never explored beyond the former global actor limit");
    fixture.MoveTo(-4097); fixture.Activate();
    Check(fixture.Characters.Count() > 1, "new regions became empty after 256 cumulative actors");

    GameplaySaveData save; save.PlayerBag = items::ContainerId{1}; save.Programs = "{}";
    save.Actors = fixture.Characters.CaptureState(); save.Population = fixture.Population.CaptureState();
    const auto restored = DecodeGameplayState(EncodeGameplayState(save));
    Actors actors(fixture.CatalogValue); actors.RestoreState(restored.Actors);
    RegionalPopulation population; population.RestoreState(restored.Population, actors, fixture.CatalogValue);
    Check(actors.CaptureState().NextId == save.Actors.NextId, "dormancy lost the shared actor identity watermark");
    fixture.MoveTo(511);
    population.Activate(fixture.WorldValue, actors, fixture.CatalogValue.get(), {}, false);
    Check(actors.Find(original) && actors.Find(original)->Health->Current == injured.Health->Current,
        "long exploration changed a returning actor's identity or health");

    auto invalid = restored.Population;
    invalid.Decisions.push_back(invalid.Decisions.front());
    Reject([&] { population.RestoreState(invalid, fixture.Characters, fixture.CatalogValue); });
    invalid = restored.Population;
    invalid.Dormant.push_back(invalid.Dormant.front());
    Reject([&] { population.RestoreState(invalid, fixture.Characters, fixture.CatalogValue); });
    invalid = restored.Population;
    auto duplicated = actors.Find(original).value();
    invalid.Dormant.push_back(duplicated);
    Reject([&] { population.RestoreState(invalid, actors, fixture.CatalogValue); });
    invalid = restored.Population;
    invalid.Decisions.front().Actor.Value = save.Actors.NextId;
    Reject([&] { population.RestoreState(invalid, fixture.Characters, fixture.CatalogValue); });
    std::cout << "Explored " << total << " cumulative actors with bounded active slots; checkpoint validation passed\n";
}

void WaitingAndLegacyMigration() {
    PopulationFixture fixture;
    fixture.CatalogValue->Spawns = {{"test", 0, 0}};
    const auto surface = CreateWorldGenerator(Natural())->SurfaceHeight(511);
    fixture.WorldValue.FillRegion({480, surface - 32, 64, 32}, {});
    fixture.WorldValue.FillRegion({480, surface, 64, 16}, {CellTypeId::Stone});
    fixture.Characters.SetPlayerPosition(WorldPosition::FromCell({580, surface - 4}));
    auto &map = testing::WorldAccess::Map(fixture.WorldValue);
    auto missing = map.Remove(ChunkOf({495, surface - 4}));
    fixture.Activate();
    Check(fixture.Population.CaptureState().Decisions.empty(), "partial residency consumed candidate");
    map.Insert(std::move(missing));
    std::vector<ActorHandle> blockers;
    for (int64_t x = 494; x <= 528; x += 2)
        blockers.push_back(*fixture.Characters.Spawn(ActorKind::Resident, WorldPosition::FromCell({x, surface - 4})));
    fixture.Activate();
    Check(fixture.Population.CaptureState().Decisions.empty(), "temporary actor occupancy consumed candidate");
    for (const auto handle : blockers) fixture.Characters.Remove(handle);
    while (fixture.Characters.Count() < Actors::MaximumCount)
        blockers.push_back(*fixture.Characters.Spawn(ActorKind::Resident, WorldPosition::FromCell({580, surface - 4})));
    fixture.Activate();
    Check(fixture.Population.CaptureState().Decisions.empty(), "full actor capacity consumed candidate");
    fixture.Characters.Remove(blockers.back());
    fixture.Activate();
    const auto decisions = fixture.Population.CaptureState().Decisions;
    Check(decisions.size() == 1 && decisions.front().Actor.Value, "deferred candidate did not retry after capacity returned");
    const auto id = decisions.front().Actor;
    fixture.MoveTo(4095);
    fixture.Activate(std::span{&id, 1});
    Check(fixture.Characters.Find(id).has_value(), "referenced actor was incorrectly made dormant");

    // v1-v4 carry no population section. Initial sites stay consumed at the
    // fixed world origin even when the saved player is in a distant region.
    RegionalPopulation migrated;
    migrated.ImportLegacyScene(fixture.Characters, fixture.CatalogValue.get());
    const auto legacy = migrated.CaptureState();
    const auto entry = std::find_if(legacy.Decisions.begin(), legacy.Decisions.end(), [&](const auto &value) {
        return value.Site.Kind == ActorKind::Monster && value.Site.AnchorX == 511;
    });
    Check(entry != legacy.Decisions.end() && entry->Actor == id, "legacy migration followed the player's new location");
    GameplaySaveData old; old.PlayerBag = items::ContainerId{1}; old.Programs = "{}";
    auto wire = EncodeGameplayState(old);
    Check(wire.ends_with("1 0 0\n1 0 0\n"), "unexpected empty population encoding");
    wire.resize(wire.size() - 12); wire.replace(0, 15, "PixelGameplay 4");
    Check(DecodeGameplayState(wire).Population.Version == 0, "old gameplay format did not request migration");
    std::cout << "Partial loading, occupancy, capacity retry, pins and legacy migration passed\n";
}

void LegacyNarrowWindow() {
    Environment window("PIXEL_SIMULATION_WINDOW", "128");
    const auto catalog = Catalog();
    for (const auto kind : {WorldGeneratorKind::Flat, WorldGeneratorKind::Natural}) {
        GameSession session(SessionMode::Temporary, {kind, 1, 42}, catalog);
        const auto actors = session.GetActors().Snapshot();
        for (const auto species : {"flying_eye", "slime", "rat", "bat"})
            Check(std::any_of(actors.begin(), actors.end(), [&](const auto &actor) {
                return actor.Monster && actor.Monster->Definition == species;
            }), "legacy narrow-window scene lost an available edge spawn");
    }
}
} // namespace

int main() {
    try {
        ExplorationAndCheckpoint();
        CapacityDormancyAndValidation();
        WaitingAndLegacyMigration();
        LegacyNarrowWindow();
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
