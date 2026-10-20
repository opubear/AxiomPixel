#include "GameSession.h"
#include "Performance/FrameProfiler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <random>
#include <exception>
#include <functional>
#include <stdexcept>

namespace pixel {
namespace {
uint64_t RuntimeNumber(const char *name, uint64_t fallback) {
    const auto *value = std::getenv(name);
    if (!value || !*value)
        return fallback;
    size_t consumed = 0;
    const auto number = std::stoull(value, &consumed);
    if (value[consumed] || value[0] == '-')
        throw std::invalid_argument(std::string("invalid world setting: ") + name);
    return number;
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
        throw std::runtime_error("invalid saved wind boundary mode");
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
        throw std::runtime_error("invalid saved wind strategy");
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
} // namespace

namespace {
WorldRect InitialWindow(WorldPosition player, uint32_t edge) {
    const auto cell = player.Cell();
    const auto align = [&](int64_t value) {
        return FloorDivide(value - edge / 2 + storage::RegionEdge / 2, storage::RegionEdge) * storage::RegionEdge;
    };
    return {align(cell.X), align(cell.Y), edge, edge};
}

void CommitTextFile(const std::filesystem::path &path, const std::function<void(std::ostream &)> &write) {
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    std::ofstream output(temporary, std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    write(output);
    output.close();
    std::filesystem::rename(temporary, path);
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
    ValidateGenerationSettings(generation);
    if (GenerationFingerprint(generation) != fingerprint)
        throw std::runtime_error("world generation descriptor fingerprint mismatch");
    return generation;
}
} // namespace

WorldGenerationSettings GameSession::DefaultGeneration() {
    WorldGenerationSettings settings;
    settings.Kind = WorldGeneratorKind::Natural;
    if (const auto *kind = std::getenv("PIXEL_WORLD_GENERATOR")) {
        const std::string value(kind);
        if (value == "flat") settings.Kind = WorldGeneratorKind::Flat;
        else if (value != "natural") throw std::invalid_argument("PIXEL_WORLD_GENERATOR must be flat or natural");
    }
    if (std::getenv("PIXEL_WORLD_SEED")) {
        settings.Seed = RuntimeNumber("PIXEL_WORLD_SEED", 0);
    } else {
        std::random_device random;
        settings.Seed = (uint64_t(random()) << 32) ^ uint64_t(random());
    }
    return settings;
}

GameSession::GameSession(bool persistent, WorldGenerationSettings generation)
    : Persistent(persistent), NextGeneration(generation) {
    ValidateGenerationSettings(generation);
    if (Persistent) {
        const auto edge = RuntimeNumber("PIXEL_SIMULATION_WINDOW", storage::DefaultWindowEdge);
        if (edge > 2048)
            throw std::invalid_argument("simulation window must not exceed 2048 cells");
        StreamConfiguration.WindowEdge = uint32_t(edge);
        const auto budget = RuntimeNumber("PIXEL_WORLD_BUDGET_MIB", 512);
        if (budget > UINT64_MAX / (1024 * 1024))
            throw std::invalid_argument("world budget exceeds supported range");
        StreamConfiguration.BudgetBytes = budget * 1024 * 1024;
        const auto *root = std::getenv("PIXEL_SAVE_ROOT");
        SaveRoot = root && *root ? root : "Saves";
        std::filesystem::create_directories(SaveRoot);
        if (Load())
            return;
    }
    Reset();
}

void GameSession::InitializeScene() {
    const auto &generation = StreamConfiguration.Generation;
    const auto surface = Persistent ? CreateWorldGenerator(generation)->SurfaceHeight(512) : int64_t(512);
    GameWorld.SpawnPlayer(511, double(surface - int64_t(Character::Height)));
    GameWorld.LoadSimulationNow();
    const auto feet = GameWorld.GetPlayer()->GetWorldPosition().Cell();
    for (int y = 0; y < int(Character::Height); ++y)
        for (int x = 0; x < int(Character::Width); ++x) {
            const auto cell = GameWorld.TryGetCell(feet.X + x, feet.Y + y);
            if (!cell || cell->Type != CellTypeId::Air)
                throw std::runtime_error("generated spawn is not dry and clear");
        }
    for (int x = 0; x < int(Character::Width); ++x) {
        const auto support = GameWorld.TryGetCell(feet.X + x, surface);
        if (!support || !BlocksCharacter(support->Type))
            throw std::runtime_error("generated spawn has no solid support");
    }
    WindKind = WindStrategyKind::Empty;
    Paused = true;
    StepRequested = false;
    SimulationTime = 0;
    WindStrategySettings wind;
    wind.Kind = WindKind;
    GameWorld.ConfigureWindDomain(generation.SurfaceY - (generation.Kind == WorldGeneratorKind::Natural ? 152 : 88), wind);
}

void GameSession::Reset() {
    ValidateGenerationSettings(NextGeneration);
    if (!Persistent) {
        // Finite fixtures retain their original flat layout and dimensions.
        StreamConfiguration.Generation = {};
        ReplacingWorld();
        GameWorld.GenerateTerrain(512);
        InitializeScene();
        return;
    }
    if (!Ready)
        throw std::runtime_error("world session requires recovery before creating another world");
    const auto previous = SaveDirectory;
    if (!previous.empty())
        Save();
    const auto id = std::chrono::system_clock::now().time_since_epoch().count();
    auto candidate = SaveRoot / ("world-" + std::to_string(id));
    while (std::filesystem::exists(candidate)) candidate += "-new";
    std::filesystem::create_directories(candidate);
    // Persist the immutable recipe before the first generated chunk can exist.
    CommitTextFile(candidate / "generation.pending", [&](auto &out) { WriteGeneration(out, NextGeneration); });
    Ready = false;
    try {
        SaveDirectory = candidate;
        StreamConfiguration.Generation = NextGeneration;
        StreamConfiguration.SurfaceY = NextGeneration.SurfaceY;
        auto stream = StreamConfiguration;
        stream.Directory = candidate;
        const auto surface = CreateWorldGenerator(NextGeneration)->SurfaceHeight(512);
        const auto position = WorldPosition::FromAbsolute(511, double(surface - int64_t(Character::Height)));
        ReplacingWorld();
        GameWorld.EnableStreaming(stream, InitialWindow(position, stream.WindowEdge));
        InitializeScene();
        GameWorld.Save();
        WriteMetadata();
        CommitTextFile(SaveRoot / "current.txt", [&](auto &out) { out << candidate.filename().string() << '\n'; });
        Ready = true;
        SaveError.clear();
    } catch (...) {
        const auto failure = std::current_exception();
        GameWorld.DiscardStreaming();
        SaveDirectory = previous;
        if (!previous.empty()) {
            try { LoadDirectory(previous); }
            catch (const std::exception &error) {
                GameWorld.DiscardStreaming();
                Ready = false;
                SaveError = std::string("world recovery failed: ") + error.what();
            }
        }
        if (!Ready) Paused = true;
        std::rethrow_exception(failure);
    }
}

bool GameSession::TryReset() {
    try { Reset(); return true; }
    catch (const std::exception &error) {
        const auto recovery = !Ready ? SaveError : std::string{};
        SaveError = std::string(error.what()) + (recovery.empty() ? "" : "; " + recovery)
            + (Ready ? "" : "; session disabled until recovery");
        return false;
    }
}

bool GameSession::TryRecover() {
    if (Ready) return true;
    if (!Persistent || SaveDirectory.empty()) return false;
    try {
        ReplacingWorld();
        GameWorld.DiscardStreaming();
        LoadDirectory(SaveDirectory);
        SaveError.clear();
        return true;
    } catch (const std::exception &error) {
        GameWorld.DiscardStreaming();
        Ready = false;
        Paused = true;
        SaveError = std::string("world recovery failed: ") + error.what();
        return false;
    }
}

bool GameSession::Load() {
    std::ifstream current(SaveRoot / "current.txt");
    if (!current) {
        if (std::filesystem::exists(SaveRoot / "current.txt"))
            throw std::runtime_error("cannot read current world pointer");
        return false;
    }
    std::string id;
    current >> id;
    if (id.empty() || std::filesystem::path(id).filename() != id || id == "." || id == "..")
        throw std::runtime_error("invalid current world name");
    LoadDirectory(SaveRoot / id);
    return true;
}

void GameSession::LoadDirectory(const std::filesystem::path &directory) {
    std::ifstream input(directory / "world.meta");
    input.exceptions(std::ios::failbit | std::ios::badbit);
    std::string magic;
    uint32_t version, edge, window;
    uint64_t budget;
    WorldPosition player;
    int64_t boundary;
    bool enabled, deferredStep, paused;
    double physicsTime, remainder, simulationTime;
    input >> magic >> version >> edge >> window >> budget;
    if (magic != "AxiomPixelWorld" || (version != 2 && version != 3 && version != 4) || edge != storage::ChunkEdge)
        throw std::runtime_error("incompatible world metadata");
    const auto generation = version >= 3 ? ReadGeneration(input) : WorldGenerationSettings{};
    input >> player.Chunk.X >> player.Chunk.Y >> player.Local[0] >> player.Local[1]
        >> simulationTime >> physicsTime >> remainder >> deferredStep >> boundary >> enabled >> paused;
    if (!std::isfinite(simulationTime) || simulationTime < 0)
        throw std::runtime_error("invalid saved simulation time");
    if (version < 4 && !deferredStep && remainder >= 1.0 / 60)
        throw std::runtime_error("invalid legacy fixed simulation clock");
    player.Normalize();
    const auto wind = ReadSettings(input);
    auto stream = StreamConfiguration;
    if (!std::getenv("PIXEL_SIMULATION_WINDOW")) stream.WindowEdge = window;
    if (!std::getenv("PIXEL_WORLD_BUDGET_MIB")) stream.BudgetBytes = budget;
    stream.Generation = generation;
    stream.SurfaceY = generation.SurfaceY;
    stream.Directory = directory;
    Ready = false;
    ReplacingWorld();
    GameWorld.EnableStreaming(stream, InitialWindow(player, stream.WindowEdge));
    GameWorld.RestorePlayer(player);
    GameWorld.LoadSimulationNow();
    GameWorld.ConfigureWindDomain(boundary, wind, enabled);
    GameWorld.RestoreSimulationClock(physicsTime, remainder, deferredStep);
    SaveDirectory = directory;
    StreamConfiguration = stream;
    SimulationTime = simulationTime;
    Paused = paused;
    WindKind = wind.Kind;
    StepRequested = false;
    Ready = true;
}

void GameSession::WriteMetadata() {
    CommitTextFile(SaveDirectory / "world.meta", [&](auto &out) {
        out << "AxiomPixelWorld 4 " << storage::ChunkEdge << ' ' << StreamConfiguration.WindowEdge << ' '
            << StreamConfiguration.BudgetBytes << '\n';
        WriteGeneration(out, StreamConfiguration.Generation);
        const auto position = GameWorld.GetPlayer() ? GameWorld.GetPlayer()->GetWorldPosition() : WorldPosition{};
        out << position.Chunk.X << ' ' << position.Chunk.Y << ' ' << position.Local[0] << ' ' << position.Local[1] << '\n'
            << SimulationTime << ' ' << GameWorld.GetEnvironment().GetSimulationTime() << ' '
            << GameWorld.GetEnvironment().GetTimeRemainder() << ' ' << GameWorld.GetEnvironment().IsWaitingForBudget() << ' ' << GameWorld.GetWindBoundary() << ' '
            << GameWorld.IsWindEnabled() << ' ' << Paused << '\n';
        WriteSettings(out, GameWorld.GetWindSettings());
    });
}

void GameSession::Save() {
    if (!Persistent) return;
    if (!Ready) throw std::runtime_error("cannot save an incomplete world session");
    GameWorld.Save();
    WriteMetadata();
    SaveError.clear();
}

bool GameSession::TrySave() {
    try { Save(); return true; }
    catch (const std::exception &error) { SaveError = error.what(); return false; }
}

void GameSession::Tick(float duration, PlayerInput input) {
    if (!Ready) return;
    GameWorld.UpdateStreaming();
    if (!std::isfinite(duration) || duration <= 0) return;
    performance::FrameProfiler::Count(performance::Counter::SimulationInputMicroseconds,
                                     uint64_t(std::min(duration, .25f) * 1e6));
    GameWorld.SetPlayerInput(input.Horizontal, input.Jump);
    if (Paused && !StepRequested)
        return;
    const auto completed = GameWorld.Tick(std::min(duration, .25f), [this] {
        SimulationTime += 1.0 / 60;
        Formulas.Step(GameWorld);
    }, StepRequested);
    if (completed)
        StepRequested = false;
}

bool GameSession::SetWindStartHeight(int height) {
    if (!CanEditWorld())
        return false;
    const int64_t boundary = WorldHeight - std::clamp(height, 0, int(WorldHeight));
    GameWorld.ConfigureWindDomain(boundary, GameWorld.GetWindSettings(), height < int(WorldHeight));
    return true;
}

bool GameSession::SetWindBoundary(int64_t y) {
    if (!CanEditWorld())
        return false;
    GameWorld.ConfigureWindDomain(y, GameWorld.GetWindSettings());
    return true;
}

bool GameSession::SelectWindStrategy(WindStrategyKind kind) {
    if (!CanEditWorld())
        return false;
    auto settings = GameWorld.GetWindSettings();
    settings.Kind = kind;
    GameWorld.ConfigureWindDomain(GameWorld.GetWindBoundary(), settings, GameWorld.IsWindEnabled());
    WindKind = kind;
    if (kind == WindStrategyKind::Reference || kind == WindStrategyKind::Adaptive || kind == WindStrategyKind::Lbm)
        Paused = true;
    StepRequested = false;
    return true;
}

void GameSession::ReplacingWorld() {
    Formulas.OnWorldReplaced();
    LastAim = {1, 0};
    FormulaError.clear();
    GameWorld.ClearPlayerInput();
}

bool GameSession::CastFormulaAt(WorldPosition target) {
    FormulaError.clear();
    if (!CanEditWorld() || !GameWorld.GetPlayer()) {
        FormulaError = "World unavailable or waiting for storage budget";
        return false;
    }
    auto centre = GameWorld.GetPlayer()->GetWorldPosition();
    centre.Local[0] += Character::Width / 2.0;
    centre.Local[1] += Character::Height / 2.0;
    centre.Normalize();
    target.Normalize();
    const auto reference = centre.Cell();
    const auto mouse = target.RelativeTo(reference);
    const auto player = centre.RelativeTo(reference);
    const double dx = mouse[0] - player[0], dy = mouse[1] - player[1];
    const double length = std::hypot(dx, dy);
    if (length > 1e-9)
        LastAim = {dx / length, dy / length};

    constexpr double radius = FormulaWorldAdapter::BoltRadius;
    const double horizontal = LastAim[0] == 0 ? std::numeric_limits<double>::infinity()
        : (Character::Width / 2.0 + radius) / std::abs(LastAim[0]);
    const double vertical = LastAim[1] == 0 ? std::numeric_limits<double>::infinity()
        : (Character::Height / 2.0 + radius) / std::abs(LastAim[1]);
    const double distance = std::min(horizontal, vertical) + .25;
    const auto origin = FormulaWorldAdapter::Offset(centre, {LastAim[0] * distance, LastAim[1] * distance});
    if (FormulaWorldAdapter::Sweep(GameWorld, centre, origin).Kind != FormulaContactKind::None) {
        FormulaError = "Launch path is blocked";
        return false;
    }
    const double angle = std::atan2(LastAim[1], LastAim[0]) * 180 / std::acos(-1.0);
    if (!Formulas.Cast(GameWorld, origin, angle)) {
        FormulaError = Formulas.LastError();
        return false;
    }
    return true;
}
} // namespace pixel
