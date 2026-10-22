#include "../Support/TestAccess.h"
#include "Gameplay/GameSession.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
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

void ConvertToLegacyMetadata(const std::filesystem::path &metadata, unsigned version) {
    std::ifstream input(metadata);
    std::string header, descriptor, player, clock;
    std::getline(input, header);
    std::getline(input, descriptor);
    std::getline(input, player);
    std::getline(input, clock);
    std::ostringstream rest;
    rest << input.rdbuf();
    input.close();
    header.replace(header.find("World 5"), 7, "World " + std::to_string(version));
    double committedTime, remainder;
    std::istringstream clockInput(clock);
    clockInput >> committedTime >> remainder;
    std::string windAndPause;
    std::getline(clockInput, windAndPause);
    std::ofstream output(metadata);
    output << std::setprecision(17) << header << '\n';
    if (version >= 3) output << descriptor << '\n';
    output << player << '\n' << committedTime << ' ' << committedTime << ' ' << remainder
           << " 0" << windAndPause << '\n' << rest.str();
}

void NaturalPersistenceAndStrategySelection() {
    SaveRoot files;
    WorldGenerationSettings natural;
    natural.Kind = WorldGeneratorKind::Natural;
    natural.Seed = 42;
    std::filesystem::path original;
    {
        GameSession session(true, natural);
        Require(session.GetGenerationSettings() == natural && session.IsPaused(), "new world did not use natural strategy");
        auto &world = pixel::testing::SessionAccess::World(session);
        Require(world.GetCell(511, session.GetPlayer()->GetWorldPosition().Cell().Y + 4).Type == CellTypeId::GrassSoil,
                "natural spawn lacks a grass platform");
        world.SetCell(32, 10, {CellTypeId::Water});
        Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
        original = session.GetSaveDirectory();
    }
    {
        GameSession restored(true); // Constructor default must not override saved strategy.
        Require(restored.GetGenerationSettings() == natural, "loaded seed or strategy changed");
        Require(pixel::testing::SessionAccess::World(restored).GetCell(32, 10).Type == CellTypeId::Water, "saved water was regenerated");
        pixel::testing::SessionAccess::World(restored).SetCell(32, 10, {});
        Require(restored.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
    }
    {
        GameSession restored(true, natural);
        Require(pixel::testing::SessionAccess::World(restored).GetCell(32, 10).Type == CellTypeId::Air, "drained cell was refilled on load");
        restored.Control(pixel::NativeCaller::Driver, pixel::api::SetNextGenerator{pixel::api::GeneratorKind::Flat});
        Require(restored.Control(pixel::NativeCaller::Driver, pixel::api::ResetWorld{}).Succeeded(), "session control failed");
        Require(restored.GetGenerationSettings().Kind == WorldGeneratorKind::Flat && restored.GetSaveDirectory() != original,
                "strategy selection modified the old world instead of creating a new one");
        Require(pixel::testing::SessionAccess::World(restored).GetCell(40, 511).Type == CellTypeId::Air && pixel::testing::SessionAccess::World(restored).GetCell(40, 512).Type == CellTypeId::Dirt,
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
        Require(flat.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
    }
    // v2 has neither a generation descriptor nor the v5 committed-clock row.
    ConvertToLegacyMetadata(original / "world.meta", 2);
    WorldGenerationSettings natural;
    natural.Kind = WorldGeneratorKind::Natural;
    GameSession loaded(true, natural);
    Require(loaded.GetGenerationSettings().Kind == WorldGeneratorKind::Flat, "legacy world silently gained natural terrain");
    auto generator = CreateWorldGenerator(loaded.GetGenerationSettings());
    auto frontier = WorldStreamer::ReadChunk(original, {5, 0}, *generator);
    Require(frontier->Data().Materials[0] == CellTypeId::Air, "legacy frontier was not flat air");
    pixel::testing::SessionAccess::World(loaded).SetCell(32, 10, {CellTypeId::Dirt});
    Require(loaded.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
    std::filesystem::remove(files.Path / "current.txt");
    std::filesystem::create_directory(files.Path / "current.txt");
    Require(!loaded.Control(pixel::NativeCaller::Driver, pixel::api::ResetWorld{}).Succeeded(), "reset unexpectedly replaced a directory with the world pointer");
    Require(loaded.IsReady() && loaded.GetSaveDirectory() == original && loaded.GetGenerationSettings().Kind == WorldGeneratorKind::Flat,
            "failed candidate did not restore the old session identity");
    Require(pixel::testing::SessionAccess::World(loaded).GetCell(32, 10).Type == CellTypeId::Dirt, "candidate failure lost the old world content");
}

void FailedRecoveryIsIsolatedAndRetryable() {
    SaveRoot files;
    GameSession session(true);
    auto &world = pixel::testing::SessionAccess::World(session);
    world.SetCell(32, 10, {CellTypeId::Dirt});
    Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
    const auto directory = session.GetSaveDirectory();
    const auto chunk = directory / "chunks" / "0_0.bin";
    const auto backup = files.Path / "saved-chunk.bin";
    std::filesystem::copy_file(chunk, backup);
    { std::ofstream corrupt(chunk, std::ios::trunc); corrupt << "damaged"; }
    const auto pointer = files.Path / "current.txt";
    std::filesystem::remove(pointer);
    std::filesystem::create_directory(pointer);
    Require(!session.Control(pixel::NativeCaller::Driver, pixel::api::ResetWorld{}).Succeeded() && !session.IsReady(), "double failure did not enter Failed");
    Require(!world.GetStreamer() && world.GetMap().ChunkCount() == 0, "failed recovery retained an active streamer/map");
    Require(session.GetSaveError().find("recovery failed") != std::string::npos, "candidate error hid the recovery failure");
    Require(world.TryPlaceDirt(32, 10) == CellEditResult::NotReady &&
                world.TryRemoveMaterial(32, 10) == CellEditResult::NotReady,
            "Failed session permits editing a partial world");
    const auto time = session.GetSimulationTime();
    session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
    session.Tick(.1f, {});
    world.RequestVisibleRegion({2048, 0, 64, 64});
    world.WaitForStreaming();
    Require(session.GetSimulationTime() == time && world.GetMap().ChunkCount() == 0 && !session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(),
            "Failed session advanced, loaded chunks, or saved");
    std::filesystem::copy_file(backup, chunk, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(pointer);
    { std::ofstream current(pointer); current << directory.filename().string() << '\n'; }
    const auto metadata = directory / "world.meta";
    std::ifstream savedMetadata(metadata);
    std::ostringstream savedText;
    savedText << savedMetadata.rdbuf();
    savedMetadata.close();
    auto corruptText = savedText.str();
    const auto generationLine = corruptText.find('\n') + 1;
    const auto generationEnd = corruptText.find('\n', generationLine);
    corruptText.replace(generationLine, generationEnd - generationLine, "0 1 0 9223372036854775807 1 0");
    { std::ofstream output(metadata); output << corruptText; }
    const auto corruption = session.Control(pixel::NativeCaller::Driver, pixel::api::RecoverWorld{});
    Require(corruption.Status == pixel::api::ControlStatus::Failed && !session.IsReady()
                && corruption.Message.find("generation coordinate exceeds supported range") != std::string::npos,
            "corrupt generation must keep a recoverable session and explicit storage diagnosis");
    { std::ofstream output(metadata); output << savedText.str(); }
    Require(session.Control(pixel::NativeCaller::Driver, pixel::api::RecoverWorld{}).Succeeded() && session.IsReady() && session.GetSaveDirectory() == directory,
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
        Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
        metadata = session.GetSaveDirectory() / "world.meta";
    }
    ConvertToLegacyMetadata(metadata, 3);
    GameSession loaded(true);
    Require(loaded.GetGenerationSettings() == natural, "v3 generation descriptor was skipped by the v5 reader");
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
