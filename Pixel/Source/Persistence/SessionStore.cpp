#include "SessionStore.h"
#include "World/WorldStorageError.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pixel {
namespace {
std::filesystem::path DefaultSaveRoot() {
    const auto *root = std::getenv("PIXEL_SAVE_ROOT");
    return root && *root ? root : "Saves";
}

template <class Write>
void CommitTextFile(const std::filesystem::path &path, const Write &write) {
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    std::ofstream output(temporary, std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    write(output);
    output.close();
    std::filesystem::rename(temporary, path);
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
    if (id.empty() || std::filesystem::path(id).filename() != id || id == "." || id == "..")
        throw WorldStorageError("invalid current world name");
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
} // namespace pixel
