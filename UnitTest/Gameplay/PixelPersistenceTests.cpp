#include "../Support/TestAccess.h"
#include "Camera/GameCamera.h"
#include "Gameplay/GameSession.h"
#include "PixelRender/GamePresentation.h"
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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
        world.RestorePlayer(position);
        world.LoadSimulationNow();
        Require(world.GetSimulationBounds().X < 0 && world.TryGetCell(-24, 10), "player window did not load negative chunks");
        world.SetCell(-24, 10, {pixel::CellTypes::Dirt, {-1, 1}});
        world.SetBaseForce(-24, 10, {123.25f, -456.5f});
        session.Execute(pixel::NativeCaller::Driver, pixel::api::SetWindBoundary{450});
        session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Procedural});
        savedWind = world.SampleWind(100.5, 100.5);
        savedPhysicsTime = world.GetEnvironment().GetSimulationTime();
        savedRemainder = world.GetEnvironment().GetTimeRemainder();
        Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
        original = session.GetSaveDirectory();
    }
    {
        pixel::GameSession restored(true);
        auto &world = pixel::testing::SessionAccess::World(restored);
        Require(restored.GetSaveDirectory() == original, "startup did not reopen current world");
        Require(world.GetCell(-24, 10) == pixel::CellState{pixel::CellTypes::Dirt, {-1, 1}} && world.GetBaseForceField().Get(-24, 10) == pixel::ForceVector{123.25f, -456.5f},
                "session save lost map or base force");
        Require(world.GetPlayer()->GetWorldPosition().Chunk == position.Chunk && world.GetPlayer()->GetWorldPosition().Local == position.Local,
                "player sub-cell position did not round trip");
        Require(world.GetWindBoundary() == 450 && restored.GetSelectedWindStrategy() == pixel::WindStrategyKind::Procedural,
                "world environment settings did not round trip");
        const auto restoredWind = world.SampleWind(100.5, 100.5);
        Require(std::abs(restoredWind.X - savedWind.X) < 1e-5 && std::abs(restoredWind.Y - savedWind.Y) < 1e-5 && world.GetEnvironment().GetSimulationTime() == savedPhysicsTime && world.GetEnvironment().GetTimeRemainder() == savedRemainder,
                "saved procedural wind phase and fixed-step remainder diverged");
        Require(restored.Control(pixel::NativeCaller::Driver, pixel::api::ResetWorld{}).Succeeded(), "session control failed");
        Require(restored.GetSaveDirectory() != original && std::filesystem::exists(original / "world.meta"),
                "reset must create a new world and preserve old save files");
    }
}

void PublicationBudgetAndOwnership() {
    SaveRoot root;
    pixel::GameSession session(true);
    auto &world = pixel::testing::SessionAccess::World(session);
    pixel::WorldStreamSettings settings;
    settings.Directory = session.GetSaveDirectory();
    settings.BudgetBytes = 50ull * 1024 * 1024;
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

void DeferredStepSurvivesSave() {
    SaveRoot root;
    pixel::WindVelocity savedWind;
    {
        pixel::GameSession session(true);
        auto &world = pixel::testing::SessionAccess::World(session);
        pixel::WorldStreamSettings settings;
        settings.Directory = session.GetSaveDirectory();
        settings.BudgetBytes = 50ull * 1024 * 1024;
        world.EnableStreaming(settings);
        world.SpawnPlayer(511, 508);
        world.SetCell(32, 10, {pixel::CellTypes::Dirt, {0, 1}});
        session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Procedural});
        world.WaitForStreaming();
        for (auto coordinate : world.GetMap().ChunkCoordinates())
            if (!world.GetSimulationBounds().Contains({coordinate.X * pixel::storage::ChunkEdge, coordinate.Y * pixel::storage::ChunkEdge}))
                pixel::testing::WorldAccess::Map(world).Remove(coordinate);
        world.SetPublicationBytes(settings.BudgetBytes - world.GetStreamer()->RetainedBytes() - 128);
        world.Tick(1.f / 60);
        Require(world.GetEnvironment().IsWaitingForBudget(), "test did not exhaust material scratch budget");
        savedWind = world.SampleWind(100.5, 100.5);
        Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
    }
    pixel::GameSession restored(true);
    auto &world = pixel::testing::SessionAccess::World(restored);
    const auto loadedWind = world.SampleWind(100.5, 100.5);
    Require(world.GetEnvironment().IsWaitingForBudget() && std::hypot(savedWind.X - loadedWind.X, savedWind.Y - loadedWind.Y) < 1e-5,
            "deferred step or advanced procedural phase was lost during save");
    world.Tick(1.f / 60);
    Require(!world.GetEnvironment().IsWaitingForBudget() && std::abs(world.GetEnvironment().GetSimulationTime() - 1.0 / 60) < 1e-10,
            "saved deferred step advanced twice or did not resume");
}

void ManualSaveFailureCanRetry() {
    SaveRoot root;
    pixel::GameSession session(true);
    auto &world = pixel::testing::SessionAccess::World(session);
    world.SetCell(32, 10, {pixel::CellTypes::Dirt, {0, 1}});
    const auto metadata = session.GetSaveDirectory() / "world.meta";
    std::filesystem::remove(metadata);
    std::filesystem::create_directory(metadata);
    Require(!session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded() && !session.GetSaveError().empty() && world.GetCell(32, 10).Type == pixel::CellTypes::Dirt,
            "manual save failure escaped the session or lost resident data");
    std::filesystem::remove_all(metadata);
    Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded() && session.GetSaveError().empty(), "manual save did not recover after I/O was repaired");
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
    pixel::Character player;
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
    world.SpawnPlayer(191.9, 128);
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
    camera.Reset(world);
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
    world.SetPlayerInput(1, false);
    const auto steps = world.Tick(2.f / 60, [&] {
        world.SetCell(10, 10, {pixel::CellTypeId::Dirt, {0, 1}});
        discardOptional();
        world.SetPublicationBytes(settings.BudgetBytes - world.GetStreamer()->RetainedBytes() - 128);
    });
    Require(steps == 1 && world.GetEnvironment().IsWaitingForBudget()
                && world.GetPlayer()->GetWorldPosition().Cell().X >= 192,
            "partial-step fixture did not cross the streaming threshold before waiting");
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
    world.ClearPlayerInput();
    Require(world.Tick(1.f / 60, {}, true) == 1 && !world.GetEnvironment().IsWaitingForBudget()
                && std::abs(world.GetEnvironment().GetSimulationTime() - 2.0 / 60) < 1e-10,
            "deferred material step did not finish exactly once");
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
        world.RestoreSimulationClock(0, .1, true);
        Require(!session.Execute(pixel::NativeCaller::Driver, pixel::api::SetWindBoundary{500}).Applied() && !session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Coarse}).Applied(),
                "waiting step permits wind backend replacement");
        Require(!session.Execute(pixel::NativeCaller::Driver, pixel::api::ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}}).Applied(), "waiting step accepted a new cast");
        session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
        session.Tick(.2f, {});
        Require(!world.GetEnvironment().IsWaitingForBudget()
                    && std::abs(world.GetEnvironment().GetTimeRemainder() - (.1 - 1.0 / 60)) < 1e-10,
                "single-step did not preserve unadvanced backlog");
        Require(session.Control(pixel::NativeCaller::Driver, pixel::api::SaveWorld{}).Succeeded(), "session control failed");
    }
    pixel::GameSession loaded(true);
    auto &world = pixel::testing::SessionAccess::World(loaded);
    Require(loaded.GetFormulaRuntime().Projectiles().empty(), "live formula instances entered a world save");
    Require(!world.GetEnvironment().IsWaitingForBudget()
                && std::abs(world.GetEnvironment().GetTimeRemainder() - (.1 - 1.0 / 60)) < 1e-10,
            "v4 failed to restore complete backlog without preadvanced wind");
    loaded.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    loaded.Tick(.2f, {});
    Require(std::abs(world.GetEnvironment().GetSimulationTime() - 2.0 / 60) < 1e-10
                && std::abs(world.GetEnvironment().GetTimeRemainder() - (.1 - 2.0 / 60)) < 1e-10,
            "restored backlog advanced wind twice or drained more than one step");
}
} // namespace

int main() {
    try {
        SessionRoundTrip();
        DistantCameraAndCollision();
        PublicationBudgetAndOwnership();
        DeferredStepSurvivesSave();
        ManualSaveFailureCanRetry();
        SuccessfulSingleStepBacklogRoundTrip();
        DeferredStepKeepsStreamingDomain();
        std::cout << "Persistent session and distant coordinate tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
