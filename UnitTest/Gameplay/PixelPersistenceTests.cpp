#include "../Support/TestAccess.h"
#include "../Support/CheckpointFiles.h"
#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/Render/GamePresentation.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

struct SaveRoot {
    std::filesystem::path Path = std::filesystem::temp_directory_path() / ("pixel-session-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::optional<std::string> Previous;
    static void Set(const char *value) {
#if defined(_WIN32)
        _putenv_s("PIXEL_SAVE_ROOT", value ? value : "");
#else
        if (value)
            setenv("PIXEL_SAVE_ROOT", value, 1);
        else
            unsetenv("PIXEL_SAVE_ROOT");
#endif
    }
    SaveRoot() {
        if (const auto *old = std::getenv("PIXEL_SAVE_ROOT"))
            Previous = old;
        Set(Path.string().c_str());
    }
    ~SaveRoot() {
        Set(Previous ? Previous->c_str() : nullptr);
        std::filesystem::remove_all(Path);
    }
};

void SessionRoundTrip() {
    SaveRoot root;
    std::filesystem::path original;
    pixel::WindVelocity savedWind;
    double savedPhysicsTime = 0, savedRemainder = 0;
    const auto rowChunk = pixel::FloorDivide(507, pixel::storage::ChunkEdge);
    const pixel::WorldPosition position{{-1, rowChunk},
                                        {double(pixel::storage::ChunkEdge) - 23.875, 507.75 - double(rowChunk) * pixel::storage::ChunkEdge}};
    {
        pixel::GameSession session(true);
        auto &world = pixel::testing::SessionAccess::World(session);
        session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
        for (int i = 0; i < 60; ++i)
            session.Tick(1.f / 60, {});
        session.Tick(1.f / 120, {});
        session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{true});
        pixel::testing::SessionAccess::SetPlayerPosition(session, position);
        world.LoadSimulationNow();
        Require(world.GetSimulationBounds().X < 0 && world.TryGetCell(-24, 10), "player window did not load negative chunks");
        world.SetCell(-24, 10, {pixel::CellTypes::Dirt, {-1, 1}});
        world.SetBaseForce(-24, 10, {123.25f, -456.5f});
        session.Execute(pixel::NativeCaller::Driver, pixel::api::SetWindBoundary{450});
        session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Procedural});
        savedWind = world.SampleWind(100.5, 100.5);
        savedPhysicsTime = session.GetSimulationTime();
        savedRemainder = session.GetTimeRemainder();
        Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
        original = session.GetSaveDirectory();
    }
    {
        pixel::GameSession restored(true);
        auto &world = pixel::testing::SessionAccess::World(restored);
        Require(restored.GetSaveDirectory() == original, "startup did not reopen current world");
        Require(world.GetCell(-24, 10) == pixel::CellState{pixel::CellTypes::Dirt, {-1, 1}} && world.GetBaseForceField().Get(-24, 10) == pixel::ForceVector{123.25f, -456.5f},
                "session save lost map or base force");
        Require(restored.GetPlayer()->GetWorldPosition().Chunk == position.Chunk && restored.GetPlayer()->GetWorldPosition().Local == position.Local,
                "player sub-cell position did not round trip");
        Require(world.GetWindBoundary() == 450 && restored.GetSelectedWindStrategy() == pixel::WindStrategyKind::Procedural,
                "world environment settings did not round trip");
        const auto restoredWind = world.SampleWind(100.5, 100.5);
        Require(std::abs(restoredWind.X - savedWind.X) < 1e-5 && std::abs(restoredWind.Y - savedWind.Y) < 1e-5 && restored.GetSimulationTime() == savedPhysicsTime && restored.GetTimeRemainder() == savedRemainder,
                "saved procedural wind phase and fixed-step remainder diverged");
        Require(restored.Control(pixel::NativeCaller::Driver, pixel::api::ResetWorld{}).Succeeded(), "session control failed");
        Require(restored.GetSaveDirectory() != original && std::filesystem::exists(pixel::testing::CheckpointDirectory(original) / "world.meta"),
                "reset must create a new world and preserve old save files");
    }
}

void PublicationBudgetAndOwnership() {
    SaveRoot root;
    pixel::GameSession session(true);
    auto &world = pixel::testing::SessionAccess::World(session);
    pixel::WorldStreamSettings settings;
    settings.Directory = session.GetSaveDirectory();
    // A 1024-cell viewport needs 256 resident chunks in the chunk64 build;
    // their per-chunk I/O reservations alone leave too little of 50 MiB.
    settings.BudgetBytes = (pixel::storage::ChunkEdge == 64 ? 128ull : 50ull) * 1024 * 1024;
    world.EnableStreaming(settings);
    world.RequestVisibleRegion({0, 0, 1024, 1024});
    pixel::GameCamera camera;
    camera.SetFramebufferSize({1024, 1024});
    GridView2D view;
    view.CellSizePixels = {1, 1};
    camera.SetView(view);
    pixel::GamePresentation presentation;
    presentation.WindVisual.Mode = WindVisualMode::Off;
    WorldGridFrame frame;
    auto publish = presentation.CreateFramePublisher(session, camera);
    for (int i = 0; i < 3; ++i) {
        publish(frame);
        world.WaitForStreaming();
    }
    Require(world.GetStreamer()->RetainedBytes() <= settings.BudgetBytes,
            "publication and prefetch exceeded world storage budget");
    const auto frameBytes = world.PublicationBytesFor(&frame);
    Require(frameBytes >= frame.GetMemoryUsage().ReservedBytes(), "frame storage was not tracked independently");
    publish = {};
    Require(world.PublicationBytesFor(&frame) == frameBytes,
            "destroying publisher untracked a surviving frame allocation");
}

std::string ReadText(const std::filesystem::path &path) {
    std::ifstream input(path);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

bool SamePosition(const pixel::WorldPosition &a, const pixel::WorldPosition &b) {
    return a.Chunk == b.Chunk && a.Local == b.Local;
}

void PendingRefusesPersistence() {
    SaveRoot root;
    pixel::GameSession session(true);
    auto &world = pixel::testing::SessionAccess::World(session);
    world.SetCell(32, 10, {pixel::CellTypes::Dirt, {0, 1}});
    session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Procedural});
    Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "initial save failed");
    const auto directory = session.GetSaveDirectory();
    const auto metadata = ReadText(directory / "checkpoint.current");
    const auto pointer = ReadText(root.Path / "current.txt");
    const auto worldCount = std::distance(std::filesystem::directory_iterator(root.Path), std::filesystem::directory_iterator{});
    world.WaitForStreaming();
    for (auto coordinate : world.GetMap().ChunkCoordinates())
        if (!world.GetSimulationBounds().Contains({coordinate.X * pixel::storage::ChunkEdge, coordinate.Y * pixel::storage::ChunkEdge}))
            pixel::testing::WorldAccess::Map(world).Remove(coordinate);
    world.SetPublicationBytes(pixel::storage::DefaultBudget - world.GetStreamer()->RetainedBytes() - 128);
    session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    session.Tick(1.f / 60, {});
    Require(world.GetEnvironment().IsWaitingForBudget(), "test did not exhaust material scratch budget");
    const auto stamp = session.CurrentCommit();
    const auto remainder = session.GetTimeRemainder();
    const auto wind = world.SampleWind(100.5, 100.5);
    const auto player = session.GetPlayer()->GetWorldPosition();
    for (int retry = 0; retry < 3; ++retry) {
        for (const pixel::api::SessionControl control : {pixel::api::SessionControl{pixel::api::SaveWorld{}}, pixel::api::SessionControl{pixel::api::ResetWorld{}}}) {
            const auto result = session.Control(pixel::NativeCaller::Driver, control);
            Require(result.Status == pixel::api::ControlStatus::Deferred && !result.Succeeded()
                        && result.Rejection == pixel::api::Reason::WaitingForBudget && !result.Message.empty(),
                    "pending save/reset must return an explicit deferred result");
        }
        session.Tick(.2f, {});
        Require(session.IsPaused() && session.IsReady() && session.CurrentCommit() == stamp
                    && session.GetTimeRemainder() == remainder && session.GetSaveDirectory() == directory
                    && SamePosition(session.GetPlayer()->GetWorldPosition(), player) && world.SampleWind(100.5, 100.5) == wind
                    && world.GetCell(32, 10).Type == pixel::CellTypes::Dirt,
                "deferred request or persistent budget shortage advanced/replaced the session");
        Require(ReadText(directory / "checkpoint.current") == metadata && ReadText(root.Path / "current.txt") == pointer
                    && !std::filesystem::exists(directory / "checkpoint.current.tmp")
                    && std::distance(std::filesystem::directory_iterator(root.Path), std::filesystem::directory_iterator{}) == worldCount,
                "deferred request wrote metadata or allocated a replacement world");
    }
    world.SetPublicationBytes(0);
    session.Tick(1.f / 60, {});
    Require(session.CurrentCommit().Tick == stamp.Tick + 1 && ReadText(directory / "checkpoint.current") == metadata,
            "finishing a step must not automatically retry a deferred save");
    Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Status == pixel::api::ControlStatus::Saved,
            "explicit save retry did not succeed at the committed boundary");
}

void LegacyPendingImportsAtCommittedTime() {
    for (const unsigned version : {2u, 3u, 4u}) {
        SaveRoot root;
        std::filesystem::path directory;
        pixel::WorldPosition savedPlayer;
        pixel::WindVelocity savedWind;
        {
            pixel::GameSession session(true);
            auto &world = pixel::testing::SessionAccess::World(session);
            world.SetCell(32, 10, {pixel::CellTypes::Dirt, {0, 1}});
            session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Procedural});
            pixel::testing::SessionAccess::RestoreClock(session, .1, .05);
            savedPlayer = session.GetPlayer()->GetWorldPosition();
            savedWind = world.SampleWind(100.5, 100.5);
            Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "legacy fixture save failed");
            directory = session.GetSaveDirectory();
        }
        // Build a legacy half-step record from the committed chunk files. The
        // obsolete wall time is deliberately different from the committed time.
        pixel::testing::ExportLegacyFixture(directory);
        const auto metadata = directory / "world.meta";
        std::istringstream input(ReadText(metadata));
        std::string header, generation, player, clock;
        std::getline(input, header); std::getline(input, generation);
        std::getline(input, player); std::getline(input, clock);
        header.replace(header.find(" 5 "), 3, " " + std::to_string(version) + " ");
        double time, remainder;
        std::istringstream clockInput(clock);
        clockInput >> time >> remainder;
        std::string windAndPause;
        std::getline(clockInput, windAndPause);
        std::ofstream output(metadata, std::ios::trunc);
        output << std::setprecision(17) << header << '\n';
        if (version >= 3) output << generation << '\n';
        output << player << '\n' << "99 " << time << ' ' << remainder << " 1" << windAndPause << '\n' << input.rdbuf();
        output.close();

        pixel::GameSession restored(true);
        const auto &world = restored.GetWorld();
        Require(restored.IsPaused() && !world.GetEnvironment().IsWaitingForBudget()
                    && restored.CurrentCommit().Tick == 6 && restored.GetTimeRemainder() == .05
                    && SamePosition(restored.GetPlayer()->GetWorldPosition(), savedPlayer) && world.GetCell(32, 10).Type == pixel::CellTypes::Dirt
                    && world.SampleWind(100.5, 100.5) == savedWind
                    && world.GetEnvironment().GetWindSimulation().GetSimulationTime() == restored.GetSimulationTime(),
                "legacy pending import lost committed state/backlog or restored preadvanced wind");
        restored.Tick(.2f, {});
        Require(restored.CurrentCommit().Tick == 6 && restored.GetTimeRemainder() == .05, "paused import consumed its backlog");
        for (unsigned step = 1; step <= 3; ++step) {
            restored.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
            restored.Tick(.2f, {1, false});
            Require(restored.CurrentCommit().Tick == 6 + step
                        && std::abs(restored.GetTimeRemainder() - (.05 - step / 60.0)) < 1e-10
                        && std::abs(world.GetEnvironment().GetWindSimulation().GetSimulationTime() - restored.GetSimulationTime()) < 1e-10,
                    "legacy backlog was dropped, replayed or skipped a wind phase");
            if (step == 1)
                Require(world.GetCell(32, 10).Type == pixel::CellTypes::Empty
                            && !SamePosition(restored.GetPlayer()->GetWorldPosition(), savedPlayer),
                        "legacy owed step did not execute both materials and player");
        }
        restored.Tick(.2f, {});
        Require(restored.CurrentCommit().Tick == 9, "paused legacy backlog replayed without a request");
        Require(restored.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded()
                    && ReadText(pixel::testing::CheckpointDirectory(directory) / "world.meta").starts_with("AxiomPixelWorld 5 "), "legacy import did not upgrade to v5");
    }
}

void ManualSaveFailureCanRetry() {
    SaveRoot root;
    pixel::GameSession session(true);
    auto &world = pixel::testing::SessionAccess::World(session);
    world.SetCell(32, 10, {pixel::CellTypes::Dirt, {0, 1}});
    const auto metadata = session.GetSaveDirectory() / "checkpoint.current.tmp";
    std::filesystem::create_directory(metadata);
    Require(!session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded() && !session.GetSaveError().empty() && world.GetCell(32, 10).Type == pixel::CellTypes::Dirt,
            "manual save failure escaped the session or lost resident data");
    std::filesystem::remove_all(metadata);
    Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded() && session.GetSaveError().empty(), "manual save did not recover after I/O was repaired");
}

void SavePublishesAcceptedLoads() {
    for (const bool failChunkWrite : {false, true}) {
        SaveRoot root;
        pixel::GameSession session(true);
        auto &world = pixel::testing::SessionAccess::World(session);
        world.WaitForStreaming();
        world.SetCell(32, 10, {pixel::CellTypes::Dirt, {0, 1}});
        const auto failedWrite = world.GetStreamer()->Settings().Directory / "chunks" / "0_0.bin.tmp";
        if (failChunkWrite) std::filesystem::create_directory(failedWrite);
        auto view = session.CreateReadView();
        constexpr int64_t distantX = 100 * pixel::storage::ChunkEdge;
        session.RefreshReadView(*view, {distantX, 0, 64, 64});
        Require(!view->TryGetCell(distantX, 0), "save fixture must start with an unloaded view");
        const auto before = session.CurrentCommit();
        const auto result = session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{});
        Require(result.Succeeded() != failChunkWrite, "save fixture did not produce the expected storage result");
        Require(world.TryGetCell(distantX, 0) && view->TryGetCell(distantX, 0)
                    && view->Stamp() == session.CurrentCommit()
                    && session.CurrentCommit().Revision > before.Revision
                    && session.CurrentCommit().Tick == before.Tick,
                "save accepted a load without publishing its service revision");
        if (failChunkWrite) std::filesystem::remove(failedWrite);
    }
}

void DistantCameraAndCollision() {
    pixel::Camera2D camera;
    pixel::WorldPosition position{{int64_t{1} << 40, -(int64_t{1} << 40)}, {.125, .75}};
    camera.SetWorldPosition(position);
    camera.Translate({.25, -.125});
    Require(camera.GetWorldPosition().Local == std::array<double, 2>{.375, .625}, "distant camera pan lost fractional precision");
    auto target = camera.GetWorldPosition();
    target.Local[0] += .25;
    camera.FollowWorld(target, .1);
    Require(camera.GetWorldPosition().Local[0] > .375 && camera.GetWorldPosition().Local[0] < .625,
            "distant camera follow lost fractional target");
    pixel::WorldMap missing;
    pixel::Body player;
    player.OriginChunk = position.Chunk;
    player.X = .5;
    player.Y = .5;
    player.Tick(.1f, missing.View({position.Cell().X - 64, position.Cell().Y - 64, 128, 128}), 1, false);
    Require(player.X == .5 && player.Y == .5, "character crossed missing streamed geometry");
}
void DeferredStepKeepsStreamingDomain() {
    SaveRoot files;
    pixel::GameSession session;
    auto &world = pixel::testing::SessionAccess::World(session);
    pixel::WorldStreamSettings settings;
    settings.Directory = files.Path / "deferred-domain";
    settings.WindowEdge = 256;
    settings.BudgetBytes = 50ull * 1024 * 1024;
    settings.Generation.SurfaceY = 10000;
    world.EnableStreaming(settings);
    pixel::testing::SessionAccess::SetPlayerPosition(session, pixel::WorldPosition::FromAbsolute(191.9, 128));
    pixel::WindStrategySettings wind;
    wind.Kind = pixel::WindStrategyKind::Coarse;
    wind.Coarse.Columns = wind.Coarse.Rows = 32;
    wind.Coarse.Ambient = {};
    wind.Coarse.GustSpeed = 0;
    world.ConfigureWindDomain(256, wind);
    world.AddWindImpulse(190, 128, 16, {100, 0});
    world.WaitForStreaming();

    pixel::GameCamera camera;
    camera.SetFramebufferSize({256, 256});
    camera.Reset(world.GetSimulationBounds(), session.GetPlayer());
    pixel::GamePresentation presentation;
    presentation.WindVisual.Mode = WindVisualMode::Off;
    auto publish = presentation.CreateFramePublisher(session, camera);
    WorldGridFrame frame;
    publish(frame);
    const auto discardOptional = [&] {
        world.WaitForStreaming();
        for (const auto coordinate : world.GetMap().ChunkCoordinates())
            if (!world.GetSimulationBounds().Contains({coordinate.X * pixel::storage::ChunkEdge, coordinate.Y * pixel::storage::ChunkEdge}))
                pixel::testing::WorldAccess::Map(world).Remove(coordinate);
    };
    discardOptional();
    world.SetCell(10, 10, {pixel::CellTypeId::Dirt, {0, 1}});
    world.SetPublicationBytes(settings.BudgetBytes - world.GetStreamer()->RetainedBytes() - 128);
    session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    session.Tick(1.f / 60, {1, false});
    Require(world.GetEnvironment().IsWaitingForBudget()
                && session.GetPlayer()->GetWorldPosition().Cell().X == 191,
            "pending step moved the player before environment commit");
    const auto bounds = world.GetSimulationBounds();
    const auto domain = world.GetEnvironment().GetWindSimulation().GetDomain();
    const auto revision = world.GetEnvironment().GetWindSimulation().GetRevision();
    const auto sample = world.SampleWind(190.5, 128.5);
    world.UpdateStreaming();
    publish(frame);
    Require(world.GetSimulationBounds() == bounds && world.GetEnvironment().GetWindSimulation().GetDomain() == domain
                && world.GetEnvironment().GetWindSimulation().GetRevision() == revision && world.SampleWind(190.5, 128.5) == sample,
            "streaming/publication remapped preadvanced wind while a material step was waiting");
    world.SetPublicationBytes(0);
    session.ClearPlayerInput();
    session.Tick(1.f / 60, {});
    Require(!world.GetEnvironment().IsWaitingForBudget()
                && std::abs(session.GetSimulationTime() - 1.0 / 60) < 1e-10
                && session.GetPlayer()->GetWorldPosition().Cell().X >= 192,
            "pending step lost its frozen input or did not finish exactly once");
    world.UpdateStreaming();
    Require(world.GetSimulationBounds().X == 64 && world.GetEnvironment().GetWindSimulation().GetDomain().Origin.X == 64,
            "streaming did not recenter after successful retry");
}

void SuccessfulSingleStepBacklogRoundTrip() {
    SaveRoot root;
    {
        pixel::GameSession session(true);
        Require(session.Execute(pixel::NativeCaller::Driver, pixel::api::ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}}).Applied(), "backlog fixture failed to cast");
        auto &world = pixel::testing::SessionAccess::World(session);
        pixel::testing::SessionAccess::RestoreClock(session, 0, .1, true);
        Require(!session.Execute(pixel::NativeCaller::Driver, pixel::api::SetWindBoundary{500}).Applied() && !session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Coarse}).Applied(),
                "waiting step permits wind backend replacement");
        Require(!session.Execute(pixel::NativeCaller::Driver, pixel::api::ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}}).Applied(), "waiting step accepted a new cast");
        session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
        session.Tick(.2f, {});
        Require(!world.GetEnvironment().IsWaitingForBudget()
                    && std::abs(session.GetTimeRemainder() - (.1 - 1.0 / 60)) < 1e-10,
                "single-step did not preserve unadvanced backlog");
        Require(!session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded()
                    && session.GetSaveError().find("legacy") != std::string::npos,
                "unsupported live legacy execution was silently dropped");
        pixel::testing::SessionAccess::Runtime(session).Clear(world);
        Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
    }
    pixel::GameSession loaded(true);
    auto &world = pixel::testing::SessionAccess::World(loaded);
    Require(loaded.GetFormulaRuntime().Projectiles().empty(), "live formula instances entered a world save");
    Require(!world.GetEnvironment().IsWaitingForBudget()
                && std::abs(loaded.GetTimeRemainder() - (.1 - 1.0 / 60)) < 1e-10,
            "v5 failed to restore complete backlog without preadvanced wind");
    loaded.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    loaded.Tick(.2f, {});
    Require(std::abs(loaded.GetSimulationTime() - 2.0 / 60) < 1e-10
                && std::abs(loaded.GetTimeRemainder() - (.1 - 2.0 / 60)) < 1e-10,
            "restored backlog advanced wind twice or drained more than one step");
}
} // namespace

int main() {
    try {
        SessionRoundTrip();
        DistantCameraAndCollision();
        PublicationBudgetAndOwnership();
        PendingRefusesPersistence();
        LegacyPendingImportsAtCommittedTime();
        ManualSaveFailureCanRetry();
        SavePublishesAcceptedLoads();
        SuccessfulSingleStepBacklogRoundTrip();
        DeferredStepKeepsStreamingDomain();
        std::cout << "Persistent session and distant coordinate tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
