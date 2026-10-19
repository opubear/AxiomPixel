#include "GameSession.h"
#include "Performance/FrameProfiler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
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

GameSession::GameSession(bool persistent) : Persistent(persistent) {
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
    GameWorld.SpawnPlayer(WorldWidth / 2 - Character::Width / 2, WorldHeight - TerrainHeight - Character::Height);
    WindKind = WindStrategyKind::Empty;
    Paused = true;
    StepRequested = false;
    SimulationTime = 0;
    WindStrategySettings settings;
    settings.Kind = WindKind;
    GameWorld.ConfigureWindDomain(WorldHeight - DefaultWindStartHeight, settings);
}

void GameSession::Reset() {
    if (Persistent) {
        if (!SaveDirectory.empty())
            Save();
        const auto id = std::chrono::system_clock::now().time_since_epoch().count();
        SaveDirectory = SaveRoot / ("world-" + std::to_string(id));
        WorldStreamSettings stream = StreamConfiguration;
        stream.Directory = SaveDirectory;
        GameWorld.EnableStreaming(stream);
    } else {
        GameWorld.GenerateTerrain(WorldHeight - TerrainHeight);
    }
    InitializeScene();
    if (Persistent) {
        Save();
        std::ofstream current(SaveRoot / "current.txt");
        current.exceptions(std::ios::failbit | std::ios::badbit);
        current << SaveDirectory.filename().string() << '\n';
    }
}

bool GameSession::Load() {
    std::ifstream current(SaveRoot / "current.txt");
    if (!current)
        return false;
    std::string id;
    current >> id;
    if (id.empty() || std::filesystem::path(id).filename() != id)
        throw std::runtime_error("invalid current world name");
    SaveDirectory = SaveRoot / id;
    std::ifstream input(SaveDirectory / "world.meta");
    input.exceptions(std::ios::failbit | std::ios::badbit);
    std::string magic;
    uint32_t version, edge, window;
    uint64_t budget;
    WorldPosition player;
    int64_t boundary;
    bool enabled, deferredStep;
    double physicsTime, remainder;
    input >> magic >> version >> edge >> window >> budget;
    if (magic != "AxiomPixelWorld" || version != 2 || edge != storage::ChunkEdge)
        throw std::runtime_error("incompatible world metadata");
    input >> player.Chunk.X >> player.Chunk.Y >> player.Local[0] >> player.Local[1]
        >> SimulationTime >> physicsTime >> remainder >> deferredStep >> boundary >> enabled >> Paused;
    if (!std::isfinite(SimulationTime) || SimulationTime < 0)
        throw std::runtime_error("invalid saved simulation time");
    player.Normalize();
    const auto settings = ReadSettings(input);
    if (!std::getenv("PIXEL_SIMULATION_WINDOW"))
        StreamConfiguration.WindowEdge = window;
    if (!std::getenv("PIXEL_WORLD_BUDGET_MIB"))
        StreamConfiguration.BudgetBytes = budget;
    WorldStreamSettings stream = StreamConfiguration;
    stream.Directory = SaveDirectory;
    GameWorld.EnableStreaming(stream);
    GameWorld.RestorePlayer(player);
    GameWorld.LoadSimulationNow();
    GameWorld.ConfigureWindDomain(boundary, settings, enabled);
    GameWorld.RestoreSimulationClock(physicsTime, remainder, deferredStep);
    WindKind = settings.Kind;
    StepRequested = false;
    return true;
}

void GameSession::WriteMetadata() {
    std::ofstream output(SaveDirectory / "world.meta", std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    output << "AxiomPixelWorld 2 " << storage::ChunkEdge << ' ' << StreamConfiguration.WindowEdge << ' '
           << StreamConfiguration.BudgetBytes << '\n';
    const auto position = GameWorld.GetPlayer() ? GameWorld.GetPlayer()->GetWorldPosition() : WorldPosition{};
    output << position.Chunk.X << ' ' << position.Chunk.Y << ' ' << position.Local[0] << ' ' << position.Local[1] << '\n'
           << SimulationTime << ' ' << GameWorld.GetEnvironment().GetSimulationTime() << ' '
           << GameWorld.GetEnvironment().GetTimeRemainder() << ' ' << GameWorld.GetEnvironment().IsWaitingForBudget() << ' ' << GameWorld.GetWindBoundary() << ' '
           << GameWorld.IsWindEnabled() << ' ' << Paused << '\n';
    WriteSettings(output, GameWorld.GetWindSettings());
    output.close();
}

void GameSession::Save() {
    if (!Persistent)
        return;
    GameWorld.Save();
    WriteMetadata();
    SaveError.clear();
}

bool GameSession::TrySave() {
    try {
        Save();
        return true;
    } catch (const std::exception &error) {
        SaveError = error.what();
        return false;
    }
}

void GameSession::Tick(float duration, PlayerInput input) {
    GameWorld.UpdateStreaming();
    if (!std::isfinite(duration) || duration <= 0)
        return;
    performance::FrameProfiler::Count(performance::Counter::SimulationInputMicroseconds,
                                     uint64_t(std::min(duration, .25f) * 1e6));
    GameWorld.SetPlayerInput(input.Horizontal, input.Jump);
    if (!Paused || StepRequested) {
        const float step = StepRequested ? 1.f / 60 : std::min(duration, .25f);
        GameWorld.Tick(step);
        if (!GameWorld.GetEnvironment().IsWaitingForBudget())
            SimulationTime += step;
    }
    StepRequested = false;
}

void GameSession::SetWindStartHeight(int height) {
    const int64_t boundary = WorldHeight - std::clamp(height, 0, int(WorldHeight));
    GameWorld.ConfigureWindDomain(boundary, GameWorld.GetWindSettings(), height < int(WorldHeight));
}

void GameSession::SetWindBoundary(int64_t y) {
    GameWorld.ConfigureWindDomain(y, GameWorld.GetWindSettings());
}

void GameSession::SelectWindStrategy(WindStrategyKind kind) {
    auto settings = GameWorld.GetWindSettings();
    settings.Kind = kind;
    GameWorld.ConfigureWindDomain(GameWorld.GetWindBoundary(), settings, GameWorld.IsWindEnabled());
    WindKind = kind;
    if (kind == WindStrategyKind::Reference || kind == WindStrategyKind::Adaptive || kind == WindStrategyKind::Lbm)
        Paused = true;
    StepRequested = false;
}
} // namespace pixel
