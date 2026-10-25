#include "Runtime/Gameplay/Persistence/SessionStore.h"
#include "Runtime/Simulation/World/WorldStorageError.h"
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

template <class Action>
void RequireStorageFailure(const Action &action, const char *message) {
    Require(storage::CaptureFailure(action).has_value(), message);
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

void ImmutableCheckpointGenerations() {
    TemporaryRoot root;
    SessionStore store(root.Path);
    const auto world = store.CreateWorldDirectory(NaturalRecipe());
    const auto working = store.CreateWorkingDirectory(world);
    const auto chunk = std::filesystem::path("chunks") / "-2_3.bin";
    WriteText(working / chunk, "committed chunk bytes");
    WriteText(working / "chunks" / "-2_3.bin.tmp", "interrupted temporary bytes");
    SessionSaveData metadata;
    metadata.Generation = NaturalRecipe();
    const std::string payload("domain\0payload\n", 15);
    const auto first = store.PublishCheckpoint(world, working, metadata, payload);
    store.SelectWorld(world);
    Require(store.ResolveCheckpoint(world) == first && store.ReadGameplayState(first) == payload,
            "checkpoint payload or generation pointer did not round trip");
    Require(!std::filesystem::exists(first / "chunks" / "-2_3.bin.tmp"),
            "incomplete chunk was included in checkpoint");
    const auto firstMetadata = ReadText(first / "world.meta");
    const auto firstIndex = ReadText(first / "chunks.index");
    RequireStorageFailure([&] { store.WriteMetadata(first, metadata); },
                          "immutable generation metadata was rewritten");

    const auto resumed = store.CreateWorkingDirectory(world, first);
    Require(resumed != working && ReadText(resumed / chunk) == "committed chunk bytes",
            "working directory did not clone committed chunks");
    WriteText(resumed / chunk, "new working bytes");
    WriteText(resumed / "chunks" / "1000_-42.bin", "unloaded modified chunk");
    metadata.CommittedTime = 12.25;
    const auto second = store.PublishCheckpoint(world, resumed, metadata, "new domain payload");
    Require(first != second && store.ResolveCheckpoint(world) == second
                && store.CurrentDirectory() == world, "save did not atomically publish the new generation");
    Require(ReadText(second / chunk) == "new working bytes"
                && ReadText(second / "chunks" / "1000_-42.bin") == "unloaded modified chunk",
            "checkpoint omitted modified working files outside the loaded region");
    Require(ReadText(first / chunk) == "committed chunk bytes" && ReadText(first / "world.meta") == firstMetadata
                && ReadText(first / "chunks.index") == firstIndex && store.ReadGameplayState(first) == payload,
            "saving or editing a working copy changed an old committed generation");
    WriteText(resumed / chunk, "unsaved change after publication");
    Require(ReadText(second / chunk) == "new working bytes", "working writes changed a published chunk");
}

void LegacySourceImports() {
    TemporaryRoot root;
    SessionStore store(root.Path);
    for (const auto version : {2u, 3u, 4u, 5u}) {
        const auto source = store.CreateWorldDirectory(NaturalRecipe());
        const auto fixture = MetadataFixture(version);
        WriteText(source / "world.meta", fixture);
        std::filesystem::create_directory(source / "chunks");
        WriteText(source / "chunks" / "0_0.bin", "legacy chunk");
        Require(store.ResolveCheckpoint(source) == source, "legacy source was not resolved explicitly");
        const auto destination = store.CreateWorldDirectory(NaturalRecipe());
        const auto working = store.CreateWorkingDirectory(destination, source);
        WriteText(working / "chunks" / "0_0.bin", "imported working change");
        const auto checkpoint = store.PublishCheckpoint(destination, working, store.ReadMetadata(source), "imported domains");
        Require(store.ResolveCheckpoint(destination) == checkpoint
                    && ReadText(checkpoint / "world.meta").starts_with("AxiomPixelWorld 5 "),
                "legacy import did not create a new v5-backed checkpoint");
        Require(ReadText(source / "world.meta") == fixture
                    && ReadText(source / "chunks" / "0_0.bin") == "legacy chunk"
                    && !std::filesystem::exists(source / "checkpoint.current"),
                "legacy source was modified during explicit import");
    }
    const auto emptyLegacy = store.CreateWorldDirectory({});
    store.WriteMetadata(emptyLegacy, {});
    Require(store.ResolveCheckpoint(emptyLegacy) == emptyLegacy, "empty legacy world was rejected");
    const auto emptyWorking = store.CreateWorkingDirectory(emptyLegacy, emptyLegacy);
    Require(std::filesystem::is_empty(emptyWorking / "chunks"), "empty legacy import invented chunks");
    const auto incomplete = store.CreateWorldDirectory({});
    RequireStorageFailure([&] { store.ResolveCheckpoint(incomplete); }, "incomplete world silently became a new world");
    RequireStorageFailure([&] { store.CreateWorkingDirectory(incomplete, root.Path / "missing-source"); },
                          "missing explicit source silently became a new world");
}

void LegacyMigrationPublicationBoundaries() {
    TemporaryRoot root;
    SessionStore store(root.Path);
    const auto world = store.CreateWorldDirectory(NaturalRecipe());
    const auto fixture = MetadataFixture(4);
    WriteText(world / "world.meta", fixture);
    std::filesystem::create_directory(world / "chunks");
    WriteText(world / "chunks" / "0_0.bin", "legacy chunk");
    const auto metadata = store.ReadMetadata(world);
    const auto working = store.CreateWorkingDirectory(world, world);
    const auto pointer = world / "checkpoint.current";
    const auto temporaryPointer = world / "checkpoint.current.tmp";
    const auto checkpoints = world / "checkpoints";
    Require(store.ResolveCheckpoint(world) == world, "untouched legacy world did not resolve");

    std::filesystem::create_directory(temporaryPointer);
    RequireStorageFailure([&] { store.PublishCheckpoint(world, working, metadata, "migrated domains"); },
                          "blocked legacy bootstrap unexpectedly published");
    Require(!std::filesystem::exists(pointer) && !std::filesystem::exists(checkpoints)
                && store.ResolveCheckpoint(world) == world,
            "failed legacy bootstrap created checkpoint storage or lost the original source");
    std::filesystem::remove(temporaryPointer);
    // Represents interruption after writing the bootstrap file and before its
    // rename. No generation storage may exist yet, and root metadata is valid.
    WriteText(temporaryPointer, "AxiomPixelCheckpoint 1\nlegacy\n");
    Require(store.ResolveCheckpoint(world) == world && !std::filesystem::exists(checkpoints),
            "unpublished legacy pointer prevented recovery of original metadata");

    // Fail generation construction after bootstrap to exercise the actual
    // transition from an implicit source to an explicit legacy pointer.
    WriteText(working / "chunks" / "invalid.bin", "invalid candidate chunk");
    RequireStorageFailure([&] { store.PublishCheckpoint(world, working, metadata, "migrated domains"); },
                          "invalid legacy migration candidate unexpectedly published");
    const auto legacyPointer = ReadText(pointer);
    Require(legacyPointer == "AxiomPixelCheckpoint 1\nlegacy\n" && std::filesystem::is_directory(checkpoints)
                && store.ResolveCheckpoint(world) == world,
            "interrupted migration did not retain an explicit recoverable legacy source");
    std::filesystem::remove(working / "chunks" / "invalid.bin");

    std::filesystem::create_directory(temporaryPointer);
    RequireStorageFailure([&] { store.PublishCheckpoint(world, working, metadata, "migrated domains"); },
                          "blocked migrated checkpoint pointer unexpectedly published");
    Require(ReadText(pointer) == legacyPointer && store.ResolveCheckpoint(world) == world,
            "failed final publication lost the explicitly selected legacy source");
    size_t completeCandidates = 0;
    for (const auto &entry : std::filesystem::directory_iterator(checkpoints))
        if (std::filesystem::exists(entry.path() / "chunks.index")) ++completeCandidates;
    Require(completeCandidates == 1, "final publication failure was not reached after candidate completion");
    WriteText(world / "world.meta", "broken legacy metadata");
    RequireStorageFailure([&] { store.ResolveCheckpoint(world); }, "explicit legacy pointer bypassed metadata validation");
    WriteText(world / "world.meta", fixture);
    std::filesystem::remove(temporaryPointer);

    const auto checkpoint = store.PublishCheckpoint(world, working, metadata, "migrated domains");
    Require(store.ResolveCheckpoint(world) == checkpoint && ReadText(world / "world.meta") == fixture
                && ReadText(world / "chunks" / "0_0.bin") == "legacy chunk",
            "successful migration did not select checkpoint while preserving original legacy files");
    std::filesystem::remove(pointer);
    RequireStorageFailure([&] { store.ResolveCheckpoint(world); },
                          "missing migrated checkpoint pointer silently rolled back to legacy metadata");
}

void InterruptedCheckpointPublication() {
    TemporaryRoot root;
    SessionStore store(root.Path);
    const auto world = store.CreateWorldDirectory({});
    const auto working = store.CreateWorkingDirectory(world);
    WriteText(working / "chunks" / "0_0.bin", "old chunk");
    const auto committed = store.PublishCheckpoint(world, working, {}, "old domain payload");
    const auto pointer = ReadText(world / "checkpoint.current");
    WriteText(working / "chunks" / "0_0.bin", "new chunk");
    // The destination generation is completely written before this injected
    // pointer failure. Recovery must still use exactly the previous generation.
    std::filesystem::create_directory(world / "checkpoint.current.tmp");
    RequireStorageFailure([&] { store.PublishCheckpoint(world, working, {}, "new domain payload"); },
                          "blocked checkpoint pointer unexpectedly published");
    Require(store.ResolveCheckpoint(world) == committed && ReadText(world / "checkpoint.current") == pointer
                && ReadText(committed / "chunks" / "0_0.bin") == "old chunk"
                && store.ReadGameplayState(committed) == "old domain payload",
            "interrupted checkpoint replaced or damaged the committed generation");
    size_t generationCount = 0;
    for (const auto &entry : std::filesystem::directory_iterator(world / "checkpoints")) {
        ++generationCount;
        Require(std::filesystem::exists(entry.path() / "chunks.index"), "failure was injected before generation completed");
    }
    Require(generationCount == 2, "interrupted unpublished generation was not retained");
    std::filesystem::remove(world / "checkpoint.current.tmp");
    const auto retried = store.PublishCheckpoint(world, working, {}, "new domain payload");
    Require(retried != committed && store.ResolveCheckpoint(world) == retried,
            "explicit publication retry did not create a fresh generation");
}

void CheckpointCorruptionIsExplicit() {
    TemporaryRoot root;
    SessionStore store(root.Path);
    const auto world = store.CreateWorldDirectory({});
    const auto working = store.CreateWorkingDirectory(world);
    WriteText(working / "chunks" / "0_0.bin", "chunk bytes");
    const auto checkpoint = store.PublishCheckpoint(world, working, {}, "domain bytes");
    const auto pointer = ReadText(world / "checkpoint.current");
    for (const auto *invalid : {"", "AxiomPixelCheckpoint 2\ncheckpoint-other\n",
             "AxiomPixelCheckpoint 1\n../other\n", "AxiomPixelCheckpoint 1\ncheckpoint-foo/bar\n",
             "AxiomPixelCheckpoint 1\ncheckpoint-foo\\bar\n", "AxiomPixelCheckpoint 1\ncheckpoint-missing\n"}) {
        WriteText(world / "checkpoint.current", invalid);
        RequireStorageFailure([&] { store.ResolveCheckpoint(world); }, "invalid checkpoint pointer was accepted");
    }
    WriteText(world / "checkpoint.current", pointer + "unexpected\n");
    RequireStorageFailure([&] { store.ResolveCheckpoint(world); }, "trailing pointer data was accepted");
    auto embeddedNull = pointer;
    embeddedNull.insert(embeddedNull.size() - 1, 1, '\0');
    WriteText(world / "checkpoint.current", embeddedNull);
    RequireStorageFailure([&] { store.ResolveCheckpoint(world); }, "embedded null checkpoint name was accepted");
    WriteText(world / "checkpoint.current", pointer);

    for (const auto &file : {std::filesystem::path("world.meta"), std::filesystem::path("gameplay.state"),
                            std::filesystem::path("chunks.index"), std::filesystem::path("chunks/0_0.bin")}) {
        const auto bytes = ReadText(checkpoint / file);
        std::filesystem::remove(checkpoint / file);
        RequireStorageFailure([&] { store.ResolveCheckpoint(world); }, "missing committed file was accepted");
        WriteText(checkpoint / file, bytes);
        auto changed = bytes;
        changed[0] ^= 1;
        WriteText(checkpoint / file, changed);
        RequireStorageFailure([&] { store.ResolveCheckpoint(world); }, "corrupt committed file was accepted");
        WriteText(checkpoint / file, bytes);
    }
    WriteText(checkpoint / "chunks" / "1_0.bin", "unlisted chunk");
    RequireStorageFailure([&] { store.ResolveCheckpoint(world); }, "unlisted committed chunk was accepted");
    std::filesystem::remove(checkpoint / "chunks" / "1_0.bin");
    std::filesystem::create_symlink(checkpoint / "chunks" / "0_0.bin", working / "chunks" / "1_0.bin");
    RequireStorageFailure([&] { store.PublishCheckpoint(world, working, {}, "domains"); },
                          "symbolic link chunk escaped the immutable file boundary");
    Require(store.ResolveCheckpoint(world) == checkpoint, "failed corrupt candidate replaced committed checkpoint");
    RequireStorageFailure([&] { store.PublishCheckpoint(world, working, {}, ""); }, "empty gameplay payload was published");
    const std::string oversized(size_t(SessionStore::MaxGameplayStateBytes) + 1, 'x');
    RequireStorageFailure([&] { store.PublishCheckpoint(world, working, {}, oversized); },
                          "oversized gameplay payload was published");
}
} // namespace

int main() {
    try {
        MetadataCompatibility();
        DirectoryAndCommitBoundaries();
        ImmutableCheckpointGenerations();
        LegacySourceImports();
        LegacyMigrationPublicationBoundaries();
        InterruptedCheckpointPublication();
        CheckpointCorruptionIsExplicit();
        std::cout << "Session metadata compatibility, immutable checkpoints and file boundaries passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
