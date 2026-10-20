#include "Gameplay/GameSession.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

using namespace pixel;
namespace {
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
struct SaveRoot {
    std::filesystem::path Path = std::filesystem::temp_directory_path() / ("pixel-generation-session-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::optional<std::string> Previous;
    static void Set(const char *value) {
#if defined(_WIN32)
        _putenv_s("PIXEL_SAVE_ROOT", value ? value : "");
#else
        if (value) setenv("PIXEL_SAVE_ROOT", value, 1);
        else unsetenv("PIXEL_SAVE_ROOT");
#endif
    }
    SaveRoot() {
        if (const auto *old = std::getenv("PIXEL_SAVE_ROOT")) Previous = old;
        Set(Path.string().c_str());
    }
    ~SaveRoot() {
        Set(Previous ? Previous->c_str() : nullptr);
        std::filesystem::remove_all(Path);
    }
};

void NaturalPersistenceAndStrategySelection() {
    SaveRoot files;
    WorldGenerationSettings natural;
    natural.Kind = WorldGeneratorKind::Natural;
    natural.Seed = 42;
    std::filesystem::path original;
    {
        GameSession session(true, natural);
        Require(session.GetGenerationSettings() == natural && session.IsPaused(), "new world did not use natural strategy");
        auto &world = session.GetWorld();
        Require(world.GetCell(511, world.GetPlayer()->GetWorldPosition().Cell().Y + 4).Type == CellTypeId::GrassSoil,
                "natural spawn lacks a grass platform");
        world.SetCell(32, 10, {CellTypeId::Water});
        session.Save();
        original = session.GetSaveDirectory();
    }
    {
        GameSession restored(true); // Constructor default must not override saved strategy.
        Require(restored.GetGenerationSettings() == natural, "loaded seed or strategy changed");
        Require(restored.GetWorld().GetCell(32, 10).Type == CellTypeId::Water, "saved water was regenerated");
        restored.GetWorld().SetCell(32, 10, {});
        restored.Save();
    }
    {
        GameSession restored(true, natural);
        Require(restored.GetWorld().GetCell(32, 10).Type == CellTypeId::Air, "drained cell was refilled on load");
        restored.SetNextGenerator(WorldGeneratorKind::Flat);
        restored.Reset();
        Require(restored.GetGenerationSettings().Kind == WorldGeneratorKind::Flat && restored.GetSaveDirectory() != original,
                "strategy selection modified the old world instead of creating a new one");
        Require(restored.GetWorld().GetCell(40, 511).Type == CellTypeId::Air && restored.GetWorld().GetCell(40, 512).Type == CellTypeId::Dirt,
                "debug superflat boundary changed");
        Require(std::filesystem::exists(original / "world.meta"), "reset removed the old world");
    }
}

void LegacyFlatAndResetFailure() {
    SaveRoot files;
    std::filesystem::path original;
    {
        GameSession flat(true);
        original = flat.GetSaveDirectory();
        flat.Save();
    }
    // Produce genuine v2 metadata by removing the generation descriptor line.
    const auto metadata = original / "world.meta";
    std::ifstream input(metadata);
    std::string header, descriptor;
    std::getline(input, header);
    std::getline(input, descriptor);
    std::ostringstream rest;
    rest << input.rdbuf();
    input.close();
    header.replace(header.find("World 4"), 7, "World 2");
    { std::ofstream output(metadata); output << header << '\n' << rest.str(); }
    WorldGenerationSettings natural;
    natural.Kind = WorldGeneratorKind::Natural;
    GameSession loaded(true, natural);
    Require(loaded.GetGenerationSettings().Kind == WorldGeneratorKind::Flat, "legacy world silently gained natural terrain");
    auto generator = CreateWorldGenerator(loaded.GetGenerationSettings());
    auto frontier = WorldStreamer::ReadChunk(original, {5, 0}, *generator);
    Require(frontier->Data().Materials[0] == CellTypeId::Air, "legacy frontier was not flat air");
    loaded.GetWorld().SetCell(32, 10, {CellTypeId::Dirt});
    loaded.Save();
    std::filesystem::remove(files.Path / "current.txt");
    std::filesystem::create_directory(files.Path / "current.txt");
    Require(!loaded.TryReset(), "reset unexpectedly replaced a directory with the world pointer");
    Require(loaded.IsReady() && loaded.GetSaveDirectory() == original && loaded.GetGenerationSettings().Kind == WorldGeneratorKind::Flat,
            "failed candidate did not restore the old session identity");
    Require(loaded.GetWorld().GetCell(32, 10).Type == CellTypeId::Dirt, "candidate failure lost the old world content");
}

void FailedRecoveryIsIsolatedAndRetryable() {
    SaveRoot files;
    GameSession session(true);
    auto &world = session.GetWorld();
    world.SetCell(32, 10, {CellTypeId::Dirt});
    session.Save();
    const auto directory = session.GetSaveDirectory();
    const auto chunk = directory / "chunks" / "0_0.bin";
    const auto backup = files.Path / "saved-chunk.bin";
    std::filesystem::copy_file(chunk, backup);
    { std::ofstream corrupt(chunk, std::ios::trunc); corrupt << "damaged"; }
    const auto pointer = files.Path / "current.txt";
    std::filesystem::remove(pointer);
    std::filesystem::create_directory(pointer);
    Require(!session.TryReset() && !session.IsReady(), "double failure did not enter Failed");
    Require(!world.GetStreamer() && world.GetMap().ChunkCount() == 0, "failed recovery retained an active streamer/map");
    Require(session.GetSaveError().find("recovery failed") != std::string::npos, "candidate error hid the recovery failure");
    Require(world.TryPlaceDirt(32, 10) == CellEditResult::NotReady &&
                world.TryRemoveMaterial(32, 10) == CellEditResult::NotReady,
            "Failed session permits editing a partial world");
    const auto time = session.GetSimulationTime();
    session.SetPaused(false);
    session.Tick(.1f, {});
    world.RequestVisibleRegion({2048, 0, 64, 64});
    world.WaitForStreaming();
    Require(session.GetSimulationTime() == time && world.GetMap().ChunkCount() == 0 && !session.TrySave(),
            "Failed session advanced, loaded chunks, or saved");
    std::filesystem::copy_file(backup, chunk, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(pointer);
    { std::ofstream current(pointer); current << directory.filename().string() << '\n'; }
    Require(session.TryRecover() && session.IsReady() && session.GetSaveDirectory() == directory,
            "explicit recovery did not reactivate the original world");
    Require(world.GetCell(32, 10).Type == CellTypeId::Dirt && session.GetSaveError().empty(),
            "recovery failed to restore content or clear the error");
}
void Version3GenerationRemainsReadable() {
    SaveRoot files;
    WorldGenerationSettings natural;
    natural.Kind = WorldGeneratorKind::Natural;
    natural.Seed = 987654;
    std::filesystem::path metadata;
    {
        GameSession session(true, natural);
        session.Save();
        metadata = session.GetSaveDirectory() / "world.meta";
    }
    std::ifstream input(metadata);
    std::ostringstream contents;
    contents << input.rdbuf();
    input.close();
    auto text = contents.str();
    text.replace(text.find("World 4"), 7, "World 3");
    { std::ofstream output(metadata); output << text; }
    GameSession loaded(true);
    Require(loaded.GetGenerationSettings() == natural, "v3 generation descriptor was skipped by the v4 reader");
}
} // namespace

int main() {
    try {
        NaturalPersistenceAndStrategySelection();
        LegacyFlatAndResetFailure();
        FailedRecoveryIsIsolatedAndRetryable();
        Version3GenerationRemainsReadable();
        std::cout << "Generation strategy persistence, legacy compatibility and reset recovery passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
