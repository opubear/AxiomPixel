#include "Persistence/SessionStore.h"
#include "World/WorldStorageError.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace pixel;
namespace {
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

template <class Action>
void RequireFailure(const Action &action, const char *message) {
    try { action(); }
    catch (const std::exception &) { return; }
    throw std::runtime_error(message);
}

struct TemporaryRoot {
    std::filesystem::path Path = std::filesystem::temp_directory_path()
        / ("pixel-store-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryRoot() { std::filesystem::remove_all(Path); }
};

void WriteText(const std::filesystem::path &path, const std::string &text) {
    std::ofstream out(path, std::ios::trunc);
    out.exceptions(std::ios::failbit | std::ios::badbit);
    out << text;
}

std::string ReadText(const std::filesystem::path &path) {
    std::ifstream in(path);
    in.exceptions(std::ios::failbit | std::ios::badbit);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

WorldGenerationSettings NaturalRecipe() {
    return {WorldGeneratorKind::Natural, 1, 18446744073709551557ull, 768, false};
}

// Independent wire-format fixture: distinct settings for every wind backend and
// distant split coordinates catch reordered/omitted fields during extraction.
std::string MetadataFixture(uint32_t version) {
    std::ostringstream text;
    text << "AxiomPixelWorld " << version << ' ' << storage::ChunkEdge << " 256 123456789\n";
    if (version >= 3)
        text << "1 1 18446744073709551557 768 0 " << GenerationFingerprint(NaturalRecipe()) << '\n';
    text << "-1234567890123 2345678901234 0.125 63.75\n";
    text << (version < 5 ? "12.5 12.25 0.125 1 -23456 1 0\n" : "12.25 0.125 -23456 1 0\n");
    text << "3\n"
            "1 1.25 -2.5 3.75 4.5 0.5 11\n"
            "64 32 2.25\n"
            "0 -5.5 6.25 7.5 8.75 0.25 22\n"
            "0.125 512\n"
            "5 0.5 0.75\n"
            "1 9.5 -10.25 11.5 12.75 0.125 33\n"
            "0.0625 1024 16\n"
            "0 -13.5 14.25 15.5 16.75 0.0625 44\n"
            "0.03125\n";
    return text.str();
}

void MetadataCompatibility() {
    TemporaryRoot root;
    SessionStore store(root.Path);
    const auto directory = store.CreateWorldDirectory(NaturalRecipe());
    const auto metadata = directory / "world.meta";
    const auto fixture = MetadataFixture(5);
    WriteText(metadata, fixture);
    const auto data = store.ReadMetadata(directory);
    Require(data.WindowEdge == 256 && data.BudgetBytes == 123456789 && data.Generation == NaturalRecipe(),
            "metadata lost storage settings or generation identity");
    Require(data.Player.Chunk == CellCoord{-1234567890123, 2345678901234}
                && data.Player.Local == std::array<double, 2>{.125, 63.75}, "metadata lost distant position precision");
    Require(data.CommittedTime == 12.25 && data.TimeRemainder == .125
                && data.WindBoundary == -23456 && data.WindEnabled && !data.Paused,
            "metadata lost committed clock or session state");
    const auto &wind = data.Wind;
    Require(wind.Kind == WindStrategyKind::Hybrid && wind.Coarse.Ambient == WindVelocity{1.25f, -2.5f}
                && wind.Coarse.Seed == 11 && wind.Reference.Ambient == WindVelocity{-5.5f, 6.25f}
                && wind.Reference.Seed == 22 && wind.Adaptive.Flow.Ambient == WindVelocity{9.5f, -10.25f}
                && wind.Adaptive.Flow.Seed == 33 && wind.Lbm.Ambient == WindVelocity{-13.5f, 14.25f}
                && wind.Lbm.Seed == 44, "wind backend settings were mixed during decoding");
    store.WriteMetadata(directory, data);
    Require(ReadText(metadata) == fixture, "v5 metadata wire format changed");

    for (const auto version : {2u, 3u, 4u}) {
        WriteText(metadata, MetadataFixture(version));
        const auto legacy = store.ReadMetadata(directory);
        Require(legacy.Generation == (version == 2 ? WorldGenerationSettings{} : NaturalRecipe())
                    && legacy.CommittedTime == 12.25 && legacy.TimeRemainder == .125,
                "legacy metadata lost committed time or full owed backlog");
        store.WriteMetadata(directory, legacy);
        Require(ReadText(metadata).starts_with("AxiomPixelWorld 5 "), "legacy metadata was not saved as v5");
    }
    auto corruptGeneration = fixture;
    const auto surface = corruptGeneration.find(" 768 0 ");
    corruptGeneration.replace(surface, 7, " 9223372036854775807 0 ");
    WriteText(metadata, corruptGeneration);
    const auto corruption = storage::CaptureFailure([&] { store.ReadMetadata(directory); });
    Require(corruption && corruption->find("generation coordinate exceeds supported range") != std::string::npos,
            "corrupt generation range escaped the persistence failure boundary");
    WriteText(metadata, MetadataFixture(6));
    RequireFailure([&] { store.ReadMetadata(directory); }, "future metadata version was accepted");
    WriteText(metadata, fixture.substr(0, fixture.size() / 2));
    RequireFailure([&] { store.ReadMetadata(directory); }, "truncated metadata was accepted");
}

void DirectoryAndCommitBoundaries() {
    TemporaryRoot root;
    SessionStore store(root.Path);
    Require(!store.CurrentDirectory(), "empty store already selected a world");
    const auto original = store.CreateWorldDirectory(NaturalRecipe());
    Require(std::filesystem::exists(original / "generation.pending") && !store.CurrentDirectory(),
            "candidate did not persist its recipe before selection");
    SessionSaveData data;
    data.Generation = NaturalRecipe();
    store.WriteMetadata(original, data);
    store.SelectWorld(original);
    const auto candidate = store.CreateWorldDirectory({});
    Require(candidate != original && store.CurrentDirectory() == original,
            "creating a candidate replaced the current world");

    const auto metadata = original / "world.meta";
    const auto committed = ReadText(metadata);
    std::filesystem::create_directory(original / "world.meta.tmp");
    RequireFailure([&] { store.WriteMetadata(original, data); }, "unwritable temporary metadata succeeded");
    Require(ReadText(metadata) == committed && store.CurrentDirectory() == original,
            "failed write destroyed committed metadata or changed current world");
    std::filesystem::remove(original / "world.meta.tmp");
    store.WriteMetadata(original, data);

    const auto pointer = root.Path / "current.txt";
    for (const auto *invalid : {"", ".", "..", "../other", "nested/world"}) {
        WriteText(pointer, invalid);
        RequireFailure([&] { store.CurrentDirectory(); }, "invalid current world name was accepted");
    }
    store.SelectWorld(original);
    Require(store.CurrentDirectory() == original, "world pointer could not be repaired explicitly");
}
} // namespace

int main() {
    try {
        MetadataCompatibility();
        DirectoryAndCommitBoundaries();
        std::cout << "Session metadata compatibility and file boundaries passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
