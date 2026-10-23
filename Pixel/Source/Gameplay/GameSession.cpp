#include "GameSession.h"
#include "Construction/DeviceCosts.h"
#include "Persistence/GameplaySaveData.h"
#include "World/WorldStorageError.h"
#include "Performance/FrameProfiler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <limits>
#include <random>
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

WorldRect InitialWindow(WorldPosition player, uint32_t edge) {
    const auto cell = player.Cell();
    const auto align = [&](int64_t value) {
        return FloorDivide(value - edge / 2 + storage::RegionEdge / 2, storage::RegionEdge) * storage::RegionEdge;
    };
    return {align(cell.X), align(cell.Y), edge, edge};
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
    : NextGeneration(generation) {
    ValidateGenerationSettings(generation);
    items::ValidateCost(ForceEmitterCost());
    if (persistent) {
        const auto edge = RuntimeNumber("PIXEL_SIMULATION_WINDOW", storage::DefaultWindowEdge);
        if (edge > 2048)
            throw std::invalid_argument("simulation window must not exceed 2048 cells");
        StreamConfiguration.WindowEdge = uint32_t(edge);
        const auto budget = RuntimeNumber("PIXEL_WORLD_BUDGET_MIB", 512);
        if (budget > UINT64_MAX / (1024 * 1024))
            throw std::invalid_argument("world budget exceeds supported range");
        StreamConfiguration.BudgetBytes = budget * 1024 * 1024;
        Store.emplace();
        if (Load())
            return;
    }
    Reset();
}

void GameSession::InitializeScene() {
    const auto &generation = StreamConfiguration.Generation;
    const auto surface = Store ? CreateWorldGenerator(generation)->SurfaceHeight(512) : int64_t(512);
    Characters.SetPlayerPosition(WorldPosition::FromAbsolute(511, double(surface - int64_t(Character::Height))));
    GameWorld.SetSimulationFocus(GetPlayer()->GetWorldPosition());
    GameWorld.UpdateStreaming();
    GameWorld.LoadSimulationNow();
    const auto feet = GetPlayer()->GetWorldPosition().Cell();
    for (int y = 0; y < int(Character::Height); ++y)
        for (int x = 0; x < int(Character::Width); ++x) {
            const auto cell = GameWorld.TryGetCell(feet.X + x, feet.Y + y);
            if (!cell || cell->Type != CellTypeId::Air)
                throw std::logic_error("generated spawn is not dry and clear");
        }
    for (int x = 0; x < int(Character::Width); ++x) {
        const auto support = GameWorld.TryGetCell(feet.X + x, surface);
        if (!support || !BlocksCharacter(support->Type))
            throw std::logic_error("generated spawn has no solid support");
    }
    WindKind = WindStrategyKind::Empty;
    Simulation = {};
    WindStrategySettings wind;
    wind.Kind = WindKind;
    GameWorld.ConfigureWindDomain(generation.SurfaceY - (generation.Kind == WorldGeneratorKind::Natural ? 152 : 88), wind);
    if (CreateGardenOnReset) InitializeForceGarden();
}

void GameSession::Reset() {
    ValidateGenerationSettings(NextGeneration);
    if (!Store) {
        // Finite fixtures retain their original flat layout and dimensions.
        StreamConfiguration.Generation = {};
        ReplacingWorld();
        GameWorld.GenerateTerrain(512);
        InitializeScene();
        PublishCommit();
        return;
    }
    if (!Ready)
        throw std::logic_error("world session requires recovery before creating another world");
    const auto previous = SaveDirectory;
    if (!previous.empty())
        Save();
    const auto candidate = Store->CreateWorldDirectory(NextGeneration);
    Ready = false;
    try {
        SaveDirectory = candidate;
        StreamConfiguration.Generation = NextGeneration;
        StreamConfiguration.SurfaceY = NextGeneration.SurfaceY;
        auto stream = StreamConfiguration;
        stream.Directory = Store->CreateWorkingDirectory(candidate);
        const auto surface = CreateWorldGenerator(NextGeneration)->SurfaceHeight(512);
        const auto position = WorldPosition::FromAbsolute(511, double(surface - int64_t(Character::Height)));
        ReplacingWorld();
        GameWorld.EnableStreaming(stream, InitialWindow(position, stream.WindowEdge));
        InitializeScene();
        GameWorld.Save();
        Store->PublishCheckpoint(SaveDirectory, stream.Directory, CaptureSaveData(), EncodeGameplayState(CaptureGameplayData()));
        Store->SelectWorld(candidate);
        Ready = true;
        SaveError.clear();
        PublishCommit();
    } catch (...) {
        const auto failure = std::current_exception();
        GameWorld.DiscardStreaming();
        SaveDirectory = previous;
        if (!previous.empty()) {
            if (const auto recovery = storage::CaptureFailure([&] { LoadDirectory(previous); })) {
                GameWorld.DiscardStreaming();
                Ready = false;
                SaveError = "world recovery failed: " + *recovery;
            }
        }
        if (!Ready) Simulation.SetPaused(true);
        std::rethrow_exception(failure);
    }
}

bool GameSession::TryReset() {
    if (const auto failure = storage::CaptureFailure([&] { Reset(); })) {
        const auto recovery = !Ready ? SaveError : std::string{};
        SaveError = *failure + (recovery.empty() ? "" : "; " + recovery)
            + (Ready ? "" : "; session disabled until recovery");
        return false;
    }
    return true;
}

bool GameSession::TryRecover() {
    if (Ready) return true;
    if (!Store || SaveDirectory.empty()) return false;
    const auto failure = storage::CaptureFailure([&] {
        ReplacingWorld();
        GameWorld.DiscardStreaming();
        LoadDirectory(SaveDirectory);
    });
    if (failure) {
        GameWorld.DiscardStreaming();
        Ready = false;
        Simulation.SetPaused(true);
        SaveError = "world recovery failed: " + *failure;
        return false;
    }
    SaveError.clear();
    return true;
}

bool GameSession::Load() {
    const auto directory = Store->CurrentDirectory();
    if (!directory)
        return false;
    LoadDirectory(*directory);
    return true;
}

void GameSession::LoadDirectory(const std::filesystem::path &directory) {
    const auto checkpoint = Store->ResolveCheckpoint(directory);
    const auto data = Store->ReadMetadata(checkpoint);
    std::optional<GameplaySaveData> gameplay;
    if (checkpoint != directory) {
        try { gameplay = DecodeGameplayState(Store->ReadGameplayState(checkpoint)); }
        catch (const std::invalid_argument &error) { throw WorldStorageError(error.what()); }
        catch (const std::out_of_range &error) { throw WorldStorageError(error.what()); }
    }
    auto stream = StreamConfiguration;
    if (!std::getenv("PIXEL_SIMULATION_WINDOW")) stream.WindowEdge = data.WindowEdge;
    if (!std::getenv("PIXEL_WORLD_BUDGET_MIB")) stream.BudgetBytes = data.BudgetBytes;
    stream.Generation = data.Generation;
    stream.SurfaceY = data.Generation.SurfaceY;
    stream.Directory = Store->CreateWorkingDirectory(directory, checkpoint);
    Ready = false;
    ReplacingWorld();
    // These parameters came from disk; invalid settings are corrupt save data.
    try {
        GameWorld.EnableStreaming(stream, InitialWindow(data.Player, stream.WindowEdge));
        Characters.SetPlayerPosition(data.Player);
        GameWorld.SetSimulationFocus(data.Player);
        GameWorld.UpdateStreaming();
        GameWorld.LoadSimulationNow();
        GameWorld.ConfigureWindDomain(data.WindBoundary, data.Wind, data.WindEnabled);
        Simulation.Restore(data.CommittedTime, data.TimeRemainder);
        if (gameplay) {
            if (data.CommittedTime != gameplay->Tick * SimulationCoordinator::StepSeconds)
                throw WorldStorageError("checkpoint metadata and gameplay clocks disagree");
            RestoreGameplayData(*gameplay);
            if (GetPlayer()->GetWorldPosition().Chunk != data.Player.Chunk
                || GetPlayer()->GetWorldPosition().Local != data.Player.Local)
                throw WorldStorageError("checkpoint metadata and player position disagree");
            Simulation.RestoreCheckpoint(gameplay->Tick, gameplay->Revision, data.TimeRemainder);
        }
        GameWorld.RestorePendingEnvironmentStep(Simulation.Time(), false);
    } catch (const std::invalid_argument &error) {
        throw WorldStorageError(error.what());
    } catch (const std::out_of_range &error) {
        throw WorldStorageError(error.what());
    }
    SaveDirectory = directory;
    StreamConfiguration = stream;
    Simulation.SetPaused(data.Paused);
    WindKind = data.Wind.Kind;
    Ready = true;
    SynchronizeReadViews();
    PublishCommit();
}

SessionSaveData GameSession::CaptureSaveData() const {
    SessionSaveData data;
    data.WindowEdge = StreamConfiguration.WindowEdge;
    data.BudgetBytes = StreamConfiguration.BudgetBytes;
    data.Generation = StreamConfiguration.Generation;
    data.Player = GetPlayer() ? GetPlayer()->GetWorldPosition() : WorldPosition{};
    data.CommittedTime = Simulation.Time();
    data.TimeRemainder = Simulation.Remainder();
    data.WindBoundary = GameWorld.GetWindBoundary();
    data.WindEnabled = GameWorld.IsWindEnabled();
    data.Paused = Simulation.IsPaused();
    data.Wind = GameWorld.GetWindSettings();
    return data;
}

void GameSession::Save() {
    if (Simulation.HasPendingStep()) throw std::logic_error("save during pending simulation step");
    if (!Store) return;
    if (HasTransientAuthority()) throw std::logic_error("authority state cannot yet be checkpointed");
    if (!Ready) throw std::logic_error("cannot save an incomplete world session");
    const auto saveStart = std::chrono::steady_clock::now();
    try {
        GameWorld.Save();
    } catch (const WorldStorageError &) {
        // Loading may have completed before a chunk write failed. Publish that
        // service boundary even when persistence returns a recoverable failure.
        SynchronizeReadViews();
        throw;
    }
    SynchronizeReadViews();
    const auto captureStart = std::chrono::steady_clock::now();
    const auto metadata = CaptureSaveData();
    const auto gameplay = CaptureGameplayData();
    const auto payload = EncodeGameplayState(gameplay);
    const auto captureEnd = std::chrono::steady_clock::now();
    Store->PublishCheckpoint(SaveDirectory, GameWorld.GetStreamer()->Settings().Directory, metadata, payload);
    const auto writeEnd = std::chrono::steady_clock::now();
    SaveMetrics = {std::chrono::duration<double, std::micro>(captureEnd - captureStart).count(),
                   std::chrono::duration<double, std::micro>(writeEnd - captureEnd).count(),
                   std::chrono::duration<double, std::micro>(writeEnd - saveStart).count(), payload.size()};
    SaveError.clear();
}

bool GameSession::TrySave() {
    if (const auto failure = storage::CaptureFailure([&] { Save(); })) {
        SaveError = *failure;
        return false;
    }
    return true;
}

void GameSession::Tick(float duration, PlayerInput input) {
    if (!Ready) return;
    ServiceStreaming();
    if (!Simulation.BeginFrame(duration, input)) return;
    performance::FrameProfiler::Count(performance::Counter::SimulationInputMicroseconds,
                                     uint64_t(std::min(duration, .25f) * 1e6));
    while (Simulation.HasStep()) {
        const auto stepInput = Simulation.BeginStep();
        if (!GameWorld.StepEnvironment(Simulation.Time())) break;
        // Intent belongs to this successful step; pending retries never touch actors.
        Characters.Step(GameWorld.ReadState(), stepInput);
        if (GetPlayer()) GameWorld.SetSimulationFocus(GetPlayer()->GetWorldPosition());
        Formulas.Step(GameWorld);
        std::vector<FormulaEffectRecord> effects(Formulas.LastEffects().begin(), Formulas.LastEffects().end());
        std::vector<api::DeviceChanged> changes;
        StepDevices(effects, changes);
        StepForceGarden(effects);
        const bool singleStep = Simulation.CommitStep();
        RecordFormulaFacts(effects);
        const auto authorityChanges = ReconcileAuthority();
        RecordAuthorityFacts(authorityChanges);
        for (const auto &change : changes)
            RecordFact(CurrentCommit(), api::DeviceCause{change.Device.Ref.Id, change.Device.Instance}, change);
        PublishCommit();
        if (singleStep) break;
    }
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
        Simulation.SetPaused(true);
    Simulation.CancelStepRequest();
    return true;
}

void GameSession::ReplacingWorld() {
    RenewActionSessions();
    Observations.Reset();
    Facts.Reset(Epoch);
    Knowledge.Reset();
    Knowledge.BeginRuntime(Epoch);
    Simulation = {};
    PublishedMapVersion = PublishedWindVersion = PublishedActorVersion = PublishedDeviceVersion = 0;
    PublishedFieldSourceVersion = 0;
    for (const auto &weak : ReadViews)
        if (auto view = weak.lock()) view->CommittedAt = {};
    Formulas.OnWorldReplaced(GameWorld);
    Machines.Clear();
    Garden = {};
    Inventory.Clear();
    PlayerBag = Inventory.Create(4096);
    Formulas.BindPlayerInventory(Inventory, PlayerBag);
    LastAim = {1, 0};
    FormulaError.clear();
    Characters.Clear();
}

api::Reason GameSession::ActivateAt(WorldPosition target, api::RequestKey request) {
    FormulaError.clear();
    if (!CanEditWorld() || !GetPlayer()) {
        FormulaError = "World unavailable or waiting for storage budget";
        return api::Reason::SessionUnavailable;
    }
    auto centre = GetPlayer()->GetWorldPosition();
    centre.Local[0] += Character::Width / 2.0;
    centre.Local[1] += Character::Height / 2.0;
    centre.Normalize();
    target.Normalize();
    const auto reference = centre.Cell();
    const auto mouse = target.RelativeTo(reference);
    const auto player = centre.RelativeTo(reference);
    const double dx = mouse[0] - player[0], dy = mouse[1] - player[1];
    const double length = std::hypot(dx, dy);
    const auto aim = length > 1e-9 ? std::array<double, 2>{dx / length, dy / length} : LastAim;

    constexpr double radius = FormulaWorldAdapter::BoltRadius;
    const double horizontal = aim[0] == 0 ? std::numeric_limits<double>::infinity()
        : (Character::Width / 2.0 + radius) / std::abs(aim[0]);
    const double vertical = aim[1] == 0 ? std::numeric_limits<double>::infinity()
        : (Character::Height / 2.0 + radius) / std::abs(aim[1]);
    const double distance = std::min(horizontal, vertical) + .25;
    const auto origin = FormulaWorldAdapter::Offset(centre, {aim[0] * distance, aim[1] * distance});
    if (FormulaWorldAdapter::Sweep(GameWorld, centre, origin).Kind != FormulaContactKind::None) {
        FormulaError = "Launch path is blocked";
        return api::Reason::LaunchBlocked;
    }
    const double angle = std::atan2(aim[1], aim[0]) * 180 / std::acos(-1.0);
    if (!Formulas.Cast(GameWorld, origin, angle, request)) {
        FormulaError = Formulas.LastError();
        return api::Reason::LimitExceeded;
    }
    LastAim = aim;
    return api::Reason::None;
}
} // namespace pixel
