#include "../Support/TestAccess.h"
#include "Runtime/Render/BiomeFrame.h"
#include "Runtime/Render/ActorFrame.h"
#include "Runtime/Gameplay/Camera/GridView.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace {
using namespace pixel;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F action) {
    try { action(); } catch (const std::exception &) { return; }
    throw std::runtime_error("invalid biome input accepted");
}
std::vector<char> Read(std::string_view name) {
    std::ifstream file(std::filesystem::path(PIXEL_TEST_CONTENT) / name, std::ios::binary);
    if (!file) throw std::runtime_error("missing biome content");
    return {std::istreambuf_iterator<char>(file), {}};
}
WorldGenerationSettings Natural(uint64_t seed) { return {WorldGeneratorKind::Natural, 2, seed}; }

void ClimateContract() {
    const auto settings = Natural(42);
    bool found[3]{};
    for (int64_t x = -16384; x < 16384; x += 113) {
        const auto sample = SampleWorldBiome(settings, {x, -2000});
        Check(sample == SampleGenerationColumn(settings, x).Environment, "generation and runtime disagree on biome");
        Check(sample == SampleWorldBiome(settings, {x, 3000}), "surface biome changed with depth or query order");
        Check(sample.Regional && sample.ForestWeight + sample.DesertWeight + sample.SnowWeight == 65536, "climate weights lost normalization");
        found[size_t(sample.Biome)] = true;
    }
    Check(found[0] && found[1] && found[2], "missing a world biome");
    for (const int64_t x : {-(int64_t{1} << 55) - 7, (int64_t{1} << 55) + 9})
        Check(SampleWorldBiome(settings, {x, 0}) == SampleGenerationColumn(settings, x).Environment, "distant biome precision lost");
    Check(!SampleWorldBiome({}, {0, 0}).Regional, "flat fixture gained regional ecology");
    auto old = settings; old.Version = 1;
    Check(!SampleWorldBiome(old, {512, 0}).Regional, "legacy natural world gained v2 regions");
    Reject([&] { SampleWorldBiome(settings, {INT64_MAX, 0}); });
    Reject([&] { BiomeKey(BiomeId(99)); });
}

void PopulationContract() {
    BiomeSample forest; forest.Regional = true;
    BiomeSample desert = forest; desert.ForestWeight = 0; desert.DesertWeight = 65536; desert.Biome = BiomeId::Desert;
    const BiomeSpawnWeights forestOnly{100, 0, 0};
    for (const auto roll : std::array<uint64_t, 3>{0, 12345, UINT64_MAX}) {
        Check(AcceptBiomeSpawn(forest, forestOnly, roll), "forest species rejected in pure forest");
        Check(!AcceptBiomeSpawn(desert, forestOnly, roll), "forest species appeared in pure desert");
    }
    auto mixed = forest; mixed.ForestWeight = mixed.DesertWeight = 32768;
    Check(AcceptBiomeSpawn(mixed, forestOnly, 3276799) && !AcceptBiomeSpawn(mixed, forestOnly, 3276800), "ecotone probability not blended");
    Check(AcceptBiomeSpawn({}, {0, 0, 0}, 123), "legacy scene population changed");
    Reject([&] { AcceptBiomeSpawn(forest, {101, 0, 0}, 0); });

    const auto original = nlohmann::json::parse(Read("Monsters.json"));
    for (const auto invalid : {nlohmann::json{-1, 0, 0}, nlohmann::json{100, 1.5, 0}, nlohmann::json{100, 0}}) {
        auto json = original; json["Monsters"][0]["Gameplay"]["BiomeWeights"] = invalid;
        const auto text = json.dump();
        Reject([&] { MonsterCatalog::Parse({text.begin(), text.end()}); });
    }
}

void RegionalScene() {
    const auto catalog = MonsterCatalog::Parse(Read("Monsters.json"));
    size_t allDecorations = 0, residents = 0, renderedResidents = 0, monsters = 0;
    for (const auto biome : {BiomeId::Forest, BiomeId::Desert, BiomeId::SnowMountain}) {
        uint64_t seed = 1;
        while (seed < 10000 && SampleWorldBiome(Natural(seed), {512, 0}).Weight(biome) < 64000) ++seed;
        Check(seed < 10000, "unable to find a pure-biome spawn fixture");
        GameSession session(SessionMode::Temporary, Natural(seed), catalog);
        auto &world = testing::SessionAccess::World(session);
        const auto player = session.GetPlayer()->GetWorldPosition().Cell();
        for (const auto &actor : session.GetActors().Snapshot()) {
            if (actor.Kind == ActorKind::Resident) ++residents;
            if (!actor.Monster) continue;
            ++monsters;
            const auto &definition = catalog->Require(actor.Monster->Definition);
            const auto population = testing::SessionAccess::Population(session).CaptureState();
            const auto spawn = std::find_if(population.Decisions.begin(), population.Decisions.end(), [&](const auto &s) { return s.Actor == actor.Id; });
            Check(spawn != population.Decisions.end(), "regional monster has no durable candidate");
            const auto roll = BiomeSpawnRoll(seed, definition.Id, spawn->Site.AnchorX);
            Check(AcceptBiomeSpawn(world.SampleBiome(actor.Position.Cell()), definition.BiomeWeights, roll), "scene monster bypassed habitat rule");
        }
        auto snapshot = session.CreateReadView();
        const WorldRect bounds{256, player.Y - 100, 512, 256};
        session.RefreshReadView(*snapshot, bounds);
        Check(snapshot->IsValid() && snapshot->GenerationSettings() == Natural(seed), "committed view lost world generation identity");
        Camera2D camera;
        camera.SetWorldPosition(WorldPosition::FromCell({512, player.Y}));
        WorldGridFrame frame;
        frame.View = MakeCameraView(camera, {1024, 512}, GridView2D{{}, {2, 2}});
        BuildBiomeFrame(*snapshot, camera, {1024, 512}, frame);
        const auto sample = snapshot->SampleBiome(camera.GetWorldPosition().Cell());
        Check(frame.BiomeWeights[size_t(biome)] == sample.Weight(biome) / 65536.f, "background selection differs from committed region");
        Check(frame.DecorationDraws.size() <= MaximumBiomeDecorationParts, "unbounded decorations");
        allDecorations += frame.DecorationDraws.size();
        BuildActorFrame(snapshot->GetActors(), camera, {1024, 512}, frame);
        const WorldRect visible{256, player.Y - 128, 512, 256};
        for (const auto &actor : snapshot->GetActors())
            if (actor.Kind == ActorKind::Resident && visible.Contains(actor.Position.Cell())) {
                Check(std::any_of(frame.ActorDraws.begin(), frame.ActorDraws.end(), [&](const auto &d) { return d.ActorId == actor.Id.Value; }), "regional resident is invisible");
                ++renderedResidents;
            }

        // Decoration is presentation-only and must not survive destroyed support.
        world.FillRegion(bounds, {});
        testing::SessionAccess::CommitFixtureChanges(session);
        BuildBiomeFrame(*snapshot, camera, {1024, 512}, frame);
        Check(frame.DecorationDraws.empty(), "decoration floated after terrain was removed");
        frame.DisplayMode = GridDisplayMode::ForceField;
        BuildBiomeFrame(*snapshot, camera, {1024, 512}, frame);
        Check(frame.DecorationDraws.empty() && frame.BiomeWeights == std::array{1.f, 0.f, 0.f}, "biome effects leaked into F1");
    }
    Check(allDecorations > 0 && residents > 0 && renderedResidents > 0 && monsters > 0, "regional scene did not publish real content");
}
}

int main() {
    try {
        ClimateContract(); PopulationContract(); RegionalScene();
        std::cout << "Biome queries, ecology, committed presentation and support removal passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
