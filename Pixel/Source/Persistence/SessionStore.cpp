#include "SessionStore.h"
#include "World/WorldStorageError.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pixel {
namespace {
std::filesystem::path DefaultSaveRoot() {
    const auto *root = std::getenv("PIXEL_SAVE_ROOT");
    return root && *root ? root : "Saves";
}

bool IsPathComponent(const std::string &name) {
    return !name.empty() && name != "." && name != ".."
        && name.find_first_of("/\\") == std::string::npos
        && std::none_of(name.begin(), name.end(), [](unsigned char c) { return c <= 32 || c == 127; })
        && std::filesystem::path(name).filename() == name;
}

void RequireDirectory(const std::filesystem::path &path) {
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(path)))
        throw WorldStorageError("missing or invalid save directory: " + path.string());
}

void RequireFile(const std::filesystem::path &path) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
        throw WorldStorageError("missing or invalid checkpoint file: " + path.string());
}

void EnsureDirectory(const std::filesystem::path &path) {
    std::filesystem::create_directory(path);
    RequireDirectory(path);
}

std::filesystem::path UniqueDirectory(const std::filesystem::path &parent, const char *prefix) {
    RequireDirectory(parent);
    auto candidate = parent / (prefix + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    while (!std::filesystem::create_directory(candidate)) candidate += "-new";
    return candidate;
}

void RequireEnd(std::istream &input) {
    input.exceptions(std::ios::badbit);
    input >> std::ws;
    if (!input.eof()) throw WorldStorageError("unexpected checkpoint manifest data");
}

template <class Write>
void CommitTextFile(const std::filesystem::path &path, const Write &write) {
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    const auto status = std::filesystem::symlink_status(temporary);
    if (std::filesystem::exists(status) && (!std::filesystem::is_regular_file(status)
        || std::filesystem::hard_link_count(temporary) != 1))
        throw WorldStorageError("invalid temporary save file: " + temporary.string());
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    write(output);
    output.close();
    std::filesystem::rename(temporary, path);
}

bool IsChunkName(const std::string &name) {
    if (!IsPathComponent(name) || !name.ends_with(".bin")) return false;
    const auto separator = name.find('_');
    if (separator == std::string::npos) return false;
    int64_t x = 0, y = 0;
    const auto *begin = name.data(), *middle = begin + separator, *end = begin + name.size() - 4;
    const auto parsedX = std::from_chars(begin, middle, x);
    const auto parsedY = std::from_chars(middle + 1, end, y);
    return parsedX.ec == std::errc{} && parsedX.ptr == middle
        && parsedY.ec == std::errc{} && parsedY.ptr == end
        && name == std::to_string(x) + "_" + std::to_string(y) + ".bin";
}

std::vector<std::string> ChunkNames(const std::filesystem::path &directory, bool workingSource = false) {
    const auto chunks = directory / "chunks";
    if (workingSource && !std::filesystem::exists(std::filesystem::symlink_status(chunks))) return {};
    RequireDirectory(chunks);
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(chunks)) {
        const auto name = entry.path().filename().string();
        // A failed stream write may leave an incomplete temporary file. It is
        // never a saved chunk and is retained only in the source directory.
        if (workingSource && name.ends_with(".tmp") && IsChunkName(name.substr(0, name.size() - 4))) continue;
        if (!IsChunkName(name)) throw WorldStorageError("invalid saved chunk name: " + name);
        RequireFile(entry.path());
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

void CopyChunks(const std::filesystem::path &source, const std::filesystem::path &destination) {
    RequireDirectory(source);
    EnsureDirectory(destination / "chunks");
    // Independent copies also protect explicit legacy imports and accidental
    // in-place writes. Each save currently costs O(all saved chunk bytes).
    for (const auto &name : ChunkNames(source, true))
        std::filesystem::copy_file(source / "chunks" / name, destination / "chunks" / name);
}

struct FileDigest {
    uint64_t Bytes = 0;
    uint64_t Hash = 14695981039346656037ull;
    bool operator==(const FileDigest &) const = default;
};

FileDigest DigestFile(const std::filesystem::path &path) {
    RequireFile(path);
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::badbit);
    if (!input) throw WorldStorageError("cannot read checkpoint file: " + path.string());
    FileDigest digest;
    std::array<char, 64 * 1024> buffer;
    while (input) {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        digest.Bytes += uint64_t(count);
        for (std::streamsize i = 0; i < count; ++i) {
            digest.Hash ^= static_cast<unsigned char>(buffer[size_t(i)]);
            digest.Hash *= 1099511628211ull;
        }
    }
    if (!input.eof()) throw WorldStorageError("cannot read checkpoint file: " + path.string());
    return digest;
}

void WriteDigest(std::ostream &out, const std::filesystem::path &path) {
    const auto digest = DigestFile(path);
    out << digest.Bytes << ' ' << digest.Hash << '\n';
}

void VerifyDigest(std::istream &input, const std::filesystem::path &path) {
    FileDigest expected;
    input >> expected.Bytes >> expected.Hash;
    if (DigestFile(path) != expected)
        throw WorldStorageError("checkpoint file checksum mismatch: " + path.string());
}

void WriteCheckpointIndex(const std::filesystem::path &directory) {
    const auto names = ChunkNames(directory);
    CommitTextFile(directory / "chunks.index", [&](auto &out) {
        out << "AxiomPixelCheckpointFiles 1\n";
        WriteDigest(out, directory / "world.meta");
        WriteDigest(out, directory / "gameplay.state");
        out << names.size() << '\n';
        for (const auto &name : names) {
            out << name << ' ';
            WriteDigest(out, directory / "chunks" / name);
        }
    });
    if (std::filesystem::file_size(directory / "chunks.index") > SessionStore::MaxGameplayStateBytes)
        throw WorldStorageError("checkpoint file inventory exceeds size limit");
}

void VerifyCheckpointIndex(const std::filesystem::path &directory) {
    const auto path = directory / "chunks.index";
    RequireFile(path);
    if (std::filesystem::file_size(path) > SessionStore::MaxGameplayStateBytes)
        throw WorldStorageError("checkpoint file inventory exceeds size limit");
    std::ifstream input(path);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    std::string magic;
    uint32_t version = 0;
    input >> magic >> version;
    if (magic != "AxiomPixelCheckpointFiles" || version != 1)
        throw WorldStorageError("incompatible checkpoint file inventory");
    VerifyDigest(input, directory / "world.meta");
    VerifyDigest(input, directory / "gameplay.state");
    const auto names = ChunkNames(directory);
    uint64_t count = 0;
    input >> count;
    if (count != names.size()) throw WorldStorageError("checkpoint chunk inventory mismatch");
    for (const auto &expected : names) {
        std::string name;
        input >> name;
        if (name != expected) throw WorldStorageError("checkpoint chunk inventory mismatch");
        VerifyDigest(input, directory / "chunks" / name);
    }
    RequireEnd(input);
}

template <class Settings>
void WriteFlow(std::ostream &out, const Settings &settings) {
    out << int(settings.Boundary) << ' ' << settings.Ambient.X << ' ' << settings.Ambient.Y << ' '
        << settings.GustSpeed << ' ' << settings.Viscosity << ' ' << settings.Relaxation << ' ' << settings.Seed << '\n';
}

template <class Settings>
void ReadFlow(std::istream &in, Settings &settings) {
    int boundary;
    in >> boundary >> settings.Ambient.X >> settings.Ambient.Y >> settings.GustSpeed
        >> settings.Viscosity >> settings.Relaxation >> settings.Seed;
    if (boundary < int(WindBoundaryMode::Open) || boundary > int(WindBoundaryMode::Periodic))
        throw WorldStorageError("invalid saved wind boundary mode");
    settings.Boundary = WindBoundaryMode(boundary);
}

void WriteSettings(std::ostream &out, const WindStrategySettings &settings) {
    out << int(settings.Kind) << '\n';
    WriteFlow(out, settings.Coarse);
    out << settings.Coarse.Columns << ' ' << settings.Coarse.Rows << ' ' << settings.Coarse.DirtDrag << '\n';
    WriteFlow(out, settings.Reference);
    out << settings.Reference.DivergenceTolerance << ' ' << settings.Reference.MaxPressureIterations << '\n';
    out << settings.Local.Radius << ' ' << settings.Local.Deflection << ' ' << settings.Local.WakeStrength << '\n';
    WriteFlow(out, settings.Adaptive.Flow);
    out << settings.Adaptive.Flow.DivergenceTolerance << ' ' << settings.Adaptive.Flow.MaxPressureIterations << ' '
        << settings.Adaptive.BlockSize << '\n';
    WriteFlow(out, settings.Lbm);
    out << settings.Lbm.LatticeSeconds << '\n';
}

WindStrategySettings ReadSettings(std::istream &in) {
    WindStrategySettings settings;
    int kind;
    in >> kind;
    if (kind < int(WindStrategyKind::Coarse) || kind > int(WindStrategyKind::Empty))
        throw WorldStorageError("invalid saved wind strategy");
    settings.Kind = WindStrategyKind(kind);
    ReadFlow(in, settings.Coarse);
    in >> settings.Coarse.Columns >> settings.Coarse.Rows >> settings.Coarse.DirtDrag;
    ReadFlow(in, settings.Reference);
    in >> settings.Reference.DivergenceTolerance >> settings.Reference.MaxPressureIterations;
    in >> settings.Local.Radius >> settings.Local.Deflection >> settings.Local.WakeStrength;
    ReadFlow(in, settings.Adaptive.Flow);
    in >> settings.Adaptive.Flow.DivergenceTolerance >> settings.Adaptive.Flow.MaxPressureIterations >> settings.Adaptive.BlockSize;
    ReadFlow(in, settings.Lbm);
    in >> settings.Lbm.LatticeSeconds;
    return settings;
}

void WriteGeneration(std::ostream &out, const WorldGenerationSettings &generation) {
    out << uint32_t(generation.Kind) << ' ' << generation.Version << ' ' << generation.Seed << ' '
        << generation.SurfaceY << ' ' << generation.WaterEnabled << ' ' << GenerationFingerprint(generation) << '\n';
}

WorldGenerationSettings ReadGeneration(std::istream &in) {
    WorldGenerationSettings generation;
    uint32_t kind;
    uint64_t fingerprint;
    in >> kind >> generation.Version >> generation.Seed >> generation.SurfaceY >> generation.WaterEnabled >> fingerprint;
    generation.Kind = WorldGeneratorKind(kind);
    try { ValidateGenerationSettings(generation); }
    catch (const std::invalid_argument &error) { throw WorldStorageError(error.what()); }
    catch (const std::out_of_range &error) { throw WorldStorageError(error.what()); }
    if (GenerationFingerprint(generation) != fingerprint)
        throw WorldStorageError("world generation descriptor fingerprint mismatch");
    return generation;
}
} // namespace

SessionStore::SessionStore() : SessionStore(DefaultSaveRoot()) {}

SessionStore::SessionStore(std::filesystem::path root) : Root(std::move(root)) {
    std::filesystem::create_directories(Root);
}

std::optional<std::filesystem::path> SessionStore::CurrentDirectory() const {
    std::ifstream current(Root / "current.txt");
    if (!current) {
        if (std::filesystem::exists(Root / "current.txt"))
            throw WorldStorageError("cannot read current world pointer");
        return std::nullopt;
    }
    std::string id;
    current >> id;
    if (!IsPathComponent(id))
        throw WorldStorageError("invalid current world name");
    RequireEnd(current);
    return Root / id;
}

std::filesystem::path SessionStore::CreateWorldDirectory(const WorldGenerationSettings &generation) const {
    const auto id = std::chrono::system_clock::now().time_since_epoch().count();
    auto candidate = Root / ("world-" + std::to_string(id));
    while (std::filesystem::exists(candidate)) candidate += "-new";
    std::filesystem::create_directories(candidate);
    // Persist the immutable recipe before the first generated chunk can exist.
    CommitTextFile(candidate / "generation.pending", [&](auto &out) { WriteGeneration(out, generation); });
    return candidate;
}

void SessionStore::SelectWorld(const std::filesystem::path &directory) const {
    CommitTextFile(Root / "current.txt", [&](auto &out) { out << directory.filename().string() << '\n'; });
}

SessionSaveData SessionStore::ReadMetadata(const std::filesystem::path &directory) const {
    std::ifstream input(directory / "world.meta");
    input.exceptions(std::ios::failbit | std::ios::badbit);
    std::string magic;
    uint32_t version, edge;
    SessionSaveData data;
    input >> magic >> version >> edge >> data.WindowEdge >> data.BudgetBytes;
    if (magic != "AxiomPixelWorld" || version < 2 || version > SessionSaveData::CurrentVersion
        || edge != storage::ChunkEdge)
        throw WorldStorageError("incompatible world metadata");
    data.Generation = version >= 3 ? ReadGeneration(input) : WorldGenerationSettings{};
    input >> data.Player.Chunk.X >> data.Player.Chunk.Y >> data.Player.Local[0] >> data.Player.Local[1];
    if (version < 5) {
        double legacyTime;
        bool deferred;
        input >> legacyTime >> data.CommittedTime >> data.TimeRemainder >> deferred;
        if (!std::isfinite(legacyTime) || legacyTime < 0)
            throw WorldStorageError("invalid saved simulation time");
        if ((deferred && data.TimeRemainder < 1.0 / 60)
            || (version < 4 && !deferred && data.TimeRemainder >= 1.0 / 60))
            throw WorldStorageError("invalid legacy fixed simulation clock");
        // Old saves have no dynamic wind state. Keep the entire backlog and
        // rebuild at committed time; the owed step must run all domains anew.
    } else {
        input >> data.CommittedTime >> data.TimeRemainder;
    }
    input >> data.WindBoundary >> data.WindEnabled >> data.Paused;
    try { data.Player.Normalize(); }
    catch (const std::invalid_argument &error) { throw WorldStorageError(error.what()); }
    catch (const std::out_of_range &error) { throw WorldStorageError(error.what()); }
    data.Wind = ReadSettings(input);
    return data;
}

void SessionStore::WriteMetadata(const std::filesystem::path &directory, const SessionSaveData &data) const {
    if (std::filesystem::exists(directory / "chunks.index"))
        throw WorldStorageError("cannot rewrite immutable checkpoint metadata");
    CommitTextFile(directory / "world.meta", [&](auto &out) {
        out << "AxiomPixelWorld " << SessionSaveData::CurrentVersion << ' ' << storage::ChunkEdge << ' '
            << data.WindowEdge << ' ' << data.BudgetBytes << '\n';
        WriteGeneration(out, data.Generation);
        out << data.Player.Chunk.X << ' ' << data.Player.Chunk.Y << ' ' << data.Player.Local[0] << ' ' << data.Player.Local[1] << '\n'
            << data.CommittedTime << ' ' << data.TimeRemainder << ' '
            << data.WindBoundary << ' ' << data.WindEnabled << ' ' << data.Paused << '\n';
        WriteSettings(out, data.Wind);
    });
}

std::filesystem::path SessionStore::ResolveCheckpoint(const std::filesystem::path &worldDirectory) const {
    RequireDirectory(worldDirectory);
    const auto pointer = worldDirectory / "checkpoint.current";
    if (!std::filesystem::exists(std::filesystem::symlink_status(pointer))) {
        if (std::filesystem::exists(std::filesystem::symlink_status(worldDirectory / "checkpoints")))
            throw WorldStorageError("missing checkpoint pointer for checkpoint storage");
        RequireFile(worldDirectory / "world.meta");
        ReadMetadata(worldDirectory);
        return worldDirectory;
    }
    RequireFile(pointer);
    if (std::filesystem::file_size(pointer) > 1024)
        throw WorldStorageError("checkpoint pointer exceeds size limit");
    std::ifstream input(pointer);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    std::string magic, generation;
    uint32_t version = 0;
    input >> magic >> version >> generation;
    if (magic != "AxiomPixelCheckpoint" || version != 1 || !IsPathComponent(generation)
        || (generation != "legacy" && !generation.starts_with("checkpoint-")))
        throw WorldStorageError("invalid checkpoint pointer");
    RequireEnd(input);
    if (generation == "legacy") {
        RequireFile(worldDirectory / "world.meta");
        ReadMetadata(worldDirectory);
        return worldDirectory;
    }
    RequireDirectory(worldDirectory / "checkpoints");
    const auto directory = worldDirectory / "checkpoints" / generation;
    RequireDirectory(directory);
    ReadGameplayState(directory);
    VerifyCheckpointIndex(directory);
    ReadMetadata(directory);
    return directory;
}

std::filesystem::path SessionStore::CreateWorkingDirectory(const std::filesystem::path &worldDirectory,
    const std::optional<std::filesystem::path> &sourceDirectory) const {
    RequireDirectory(worldDirectory);
    EnsureDirectory(worldDirectory / "working");
    const auto directory = UniqueDirectory(worldDirectory / "working", "session-");
    if (sourceDirectory) CopyChunks(*sourceDirectory, directory);
    else EnsureDirectory(directory / "chunks");
    return directory;
}

std::filesystem::path SessionStore::PublishCheckpoint(const std::filesystem::path &worldDirectory,
    const std::filesystem::path &workingDirectory, const SessionSaveData &metadata,
    const std::string &domainPayload) const {
    if (domainPayload.empty() || domainPayload.size() > MaxGameplayStateBytes)
        throw WorldStorageError("invalid gameplay checkpoint payload size");
    RequireDirectory(worldDirectory);
    RequireDirectory(workingDirectory / "chunks");
    const auto pointer = worldDirectory / "checkpoint.current";
    const auto checkpoints = worldDirectory / "checkpoints";
    if (!std::filesystem::exists(std::filesystem::symlink_status(pointer))
        && !std::filesystem::exists(std::filesystem::symlink_status(checkpoints))
        && std::filesystem::exists(std::filesystem::symlink_status(worldDirectory / "world.meta"))) {
        RequireFile(worldDirectory / "world.meta");
        ReadMetadata(worldDirectory);
        // Establish the recoverable legacy source before any generation can
        // exist. If migration stops afterward, readers follow this explicit
        // pointer; losing a pointer can no longer trigger a silent rollback.
        CommitTextFile(pointer, [&](auto &out) { out << "AxiomPixelCheckpoint 1\nlegacy\n"; });
    }
    EnsureDirectory(checkpoints);
    const auto directory = UniqueDirectory(checkpoints, "checkpoint-");
    CopyChunks(workingDirectory, directory);
    WriteMetadata(directory, metadata);
    CommitTextFile(directory / "gameplay.state", [&](auto &out) { out << domainPayload; });
    WriteCheckpointIndex(directory);
    // This is the only operation that makes the generation visible to readers.
    // Interrupted generations and the previous committed generation are kept.
    CommitTextFile(pointer, [&](auto &out) {
        out << "AxiomPixelCheckpoint 1\n" << directory.filename().string() << '\n';
    });
    return directory;
}

std::string SessionStore::ReadGameplayState(const std::filesystem::path &checkpointDirectory) const {
    const auto path = checkpointDirectory / "gameplay.state";
    RequireFile(path);
    const auto bytes = std::filesystem::file_size(path);
    if (!bytes || bytes > MaxGameplayStateBytes)
        throw WorldStorageError("invalid gameplay checkpoint payload size");
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    std::string payload(size_t(bytes), '\0');
    input.read(payload.data(), std::streamsize(bytes));
    return payload;
}
} // namespace pixel
