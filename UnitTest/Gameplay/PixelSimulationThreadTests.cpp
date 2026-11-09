#include "../Support/TestAccess.h"
#include "Runtime/Render/GamePresentation.h"
#include <chrono>
#include <iostream>
#include <semaphore>

using namespace pixel;
namespace {
void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void Await(std::binary_semaphore &signal) {
    Require(signal.try_acquire_for(std::chrono::seconds(5)), "simulation/game workers failed to overlap");
}

void WorkerLifetimeAndFailures() {
    SimulationWorker worker;
    const auto id = worker.ThreadId();
    Require(id != std::this_thread::get_id(), "simulation runs on the caller thread");
    std::binary_semaphore simulation{0}, game{0};
    int completed = 0;
    worker.Run([&] {
        Require(std::this_thread::get_id() == id, "simulation worker changed");
        simulation.release();
        Await(game);
        ++completed;
    }, [&] { Await(simulation); game.release(); }, false);
    Require(completed == 1, "frame returned before simulation completed");
    try {
        worker.Run([&] { ++completed; }, [] { throw std::runtime_error("game failure"); }, false);
        Require(false, "game failure was lost");
    } catch (const std::runtime_error &error) {
        Require(std::string(error.what()) == "game failure" && completed == 2,
                "failed game frame did not join simulation");
    }
    try {
        worker.Invoke([] { throw std::runtime_error("simulation failure"); });
        Require(false, "simulation failure was lost");
    } catch (const std::runtime_error &error) {
        Require(std::string(error.what()) == "simulation failure", "wrong simulation exception");
    }
    Require(worker.Invoke([] { return std::this_thread::get_id(); }) == id, "worker could not finish cleanup after failure");
}

void QueuedActionsAndCompletedFrames() {
    GameSession session;
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {496, 496, 64, 64});
    session.EnableSimulationThread();
    const auto stamp = view->Stamp();
    const auto caller = session.CallerSession(NativeCaller::Driver);
    const api::ActionRequest request{session.WorldEpoch(), 1, api::MineRegion{{520, 512}}};
    const auto queued = session.Submit(caller, request);
    Require(queued.Queued() && queued.ResolvedAt.Revision == 0 && !queued.Applied(), "admission claimed application");
    Require(session.Submit(caller, request).Key == queued.Key && session.CurrentCommit() == stamp
        && view->TryGetCell(520, 512)->Type != CellTypeId::Air, "queued/duplicate action changed the world");
    session.Tick(1.f / 240, {});
    Require(session.FindAction(queued.Key)->Queued() && view->Stamp() == stamp,
            "a substep frame applied a world command");
    Require(session.Control(NativeCaller::Driver, api::SaveWorld{}).Status == api::ControlStatus::Deferred,
            "save silently discarded queued world changes");
    session.Tick(1.f / 60, {});
    const auto applied = *session.FindAction(queued.Key);
    Require(applied.Applied() && applied.ResolvedAt == session.CurrentCommit()
        && applied.ResolvedAt.Tick == stamp.Tick + 1 && view->Stamp() == applied.ResolvedAt
        && view->TryGetCell(520, 512)->Type == CellTypeId::Air, "command did not join the completed simulation frame");
    Require(session.Submit(caller, request).ResolvedAt == applied.ResolvedAt, "duplicate command was replayed");
    session.Tick(1.f / 60, {});
    Require(session.FindAction(queued.Key)->ResolvedAt == applied.ResolvedAt, "old action receipt was restamped");

    const auto wind = session.Execute(NativeCaller::Driver, api::SelectWindStrategy{api::WindKind::Empty});
    const auto windTick = session.CurrentCommit().Tick;
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(.25f, {});
    Require(session.FindAction(wind.Key)->Applied() && session.CurrentCommit().Tick == windTick + 1,
            "queued wind configuration cancelled an explicit single step");

    const auto cancelled = session.Execute(NativeCaller::Driver, api::MineRegion{{521, 512}});
    Require(session.CancelQueuedAction(cancelled.Key), "queued cancellation failed");
    session.Tick(1.f / 60, {});
    Require(!session.FindAction(cancelled.Key)->Applied() && view->TryGetCell(521, 512)->Type != CellTypeId::Air,
            "cancelled action changed terrain");
    const auto stale = session.Execute(NativeCaller::Driver, api::MineRegion{{522, 512}});
    Require(session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded(), "reset failed");
    session.Tick(1.f / 60, {});
    Require(!session.FindAction(stale.Key) && session.GetWorld().GetCell(522, 512).Type != CellTypeId::Air,
            "old epoch command escaped reset");
    Require(session.Control(NativeCaller::Driver, api::SetPaused{true}).Succeeded(), "pause failed");
    const auto frozen = session.CurrentCommit();
    session.Tick(.1f, {1, true});
    Require(session.CurrentCommit() == frozen, "paused threaded simulation advanced");
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(.1f, {});
    Require(session.CurrentCommit().Tick == frozen.Tick + 1 && session.IsPaused(), "single step lost pause semantics");
}

void BoundedQueue() {
    GameSession session;
    session.EnableSimulationThread();
    api::ActionReceipt first;
    for (int i = 0; i < 64; ++i) {
        const auto receipt = session.Execute(NativeCaller::Driver, api::MineRegion{{600 + i, 512}});
        Require(receipt.Queued(), "queue refused bounded command");
        if (!i) first = receipt;
    }
    const auto refused = session.Execute(NativeCaller::Driver, api::MineRegion{{700, 512}});
    Require(refused.Rejection == api::Reason::LimitExceeded && session.FindAction(first.Key)->Queued(),
            "queue overflow evicted unresolved work");
    session.Tick(1.f / 60, {});
    Require(session.LastAction(NativeCaller::Driver)->Key == refused.Key,
            "out-of-order completions replaced the latest receipt");
}

void BudgetRetryHoldsQueuedEffects() {
    io::TemporaryDirectory directory("pixel-simulation-thread-");
    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    WorldStreamSettings settings;
    settings.Directory = directory.Path();
    settings.WindowEdge = 256;
    settings.SurfaceY = 10000;
    settings.BudgetBytes = 256ull * 1024 * 1024;
    world.EnableStreaming(settings);
    testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromCells(100, 100));
    world.LoadSimulationNow();
    world.SetCell(32, 10, {CellTypeId::Dirt, {0, 1}});
    world.SetCell(32, 9, {CellTypeId::Fire});
    world.SetCell(40, 10, {CellTypeId::Stone});
    testing::SessionAccess::PublishFixtureChanges(session);
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {0, 0, 64, 64});
    world.WaitForStreaming();
    for (const auto coordinate : world.GetMap().ChunkCoordinates())
        if (!world.GetSimulationBounds().Contains({coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge}))
            testing::WorldAccess::Map(world).Remove(coordinate);
    testing::SessionAccess::PublishFixtureChanges(session);
    session.EnableSimulationThread();
    const auto stamp = session.CurrentCommit();
    const auto action = session.Execute(NativeCaller::Driver, api::MineRegion{{40, 10}});
    testing::SessionAccess::PrepareActions(session);
    Require(testing::SessionAccess::PreparedAction(session).Applied(), "budget fixture could not prepare mining");
    Require(world.GetCell(40, 10).Type == CellTypeId::Air && view->TryGetCell(40, 10)->Type == CellTypeId::Stone,
            "prepared action was published before the environment step");
    const auto inventory = session.GetInventory().Reference(session.PlayerInventory());
    testing::WorldAccess::SaturateBudget(world);
    session.Tick(1.f / 60, {1, false});
    Require(world.GetEnvironment().IsWaitingForBudget(), "fixture did not force budget retry");
    Require(session.CurrentCommit() == stamp && view->Stamp() == stamp && session.FindAction(action.Key)->Queued(),
            "pending simulation exposed a command result");
    Require(!session.CancelQueuedAction(action.Key), "partly applied transaction could be cancelled");
    session.Tick(.1f, {-1, true});
    Require(session.GetInventory().Reference(session.PlayerInventory()) == inventory,
            "budget retry replayed command inventory effects");
    world.SetPublicationBytes(0);
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(1.f / 60, {});
    const auto receipt = session.FindAction(action.Key);
    Require(receipt && receipt->Applied() && receipt->ResolvedAt.Tick == stamp.Tick + 1
        && view->TryGetCell(40, 10)->Type == CellTypeId::Air
        && session.GetInventory().Reference(session.PlayerInventory()) == inventory,
        "budget recovery did not commit the original command exactly once");
}

void ThreeThreadsAndPlayerPresentation() {
    GameSession session;
    session.EnableSimulationThread();
    GameCamera camera;
    camera.SetFramebufferSize({256, 256});
    camera.Reset(session.GetWorld().GetSimulationBounds(), session.GetPlayer());
    PlayerPresentation player;
    GamePresentation presentation;
    auto publish = presentation.CreateFramePublisher(session, camera, {}, &player);
    WorldGridFrame frame;
    std::thread::id gameThread;
    axiom::FramePipeline game([&](float duration) {
        gameThread = std::this_thread::get_id();
        player = session.CapturePlayerPresentation();
        const auto before = player.Player->Position;
        session.RunFrame(duration, {1, false}, [&] {
            player.Advance(duration, {1, false});
            Require(player.Player->Position.RelativeCellsTo(before.Cell())[0] > before.RelativeCellsTo(before.Cell())[0],
                    "game-owned player motion waited for authoritative publication");
            camera.FollowPlayer(&*player.Motion, duration);
        });
        player.Terrain.reset();
    });
    for (int i = 0; i < 8; ++i) {
        publish(frame);
        const std::vector<uint32_t> cells(frame.Cells().begin(), frame.Cells().end());
        const auto character = frame.Character;
        game.Run(1.f / 60, [&] {
            for (int read = 0; read < 256; ++read)
                Require(std::equal(cells.begin(), cells.end(), frame.Cells().begin())
                    && frame.Character->Bounds == character->Bounds, "worker changed a rendered frame");
        });
    }
    Require(gameThread != std::this_thread::get_id() && session.SimulationThreadId() != gameThread
        && session.SimulationThreadId() != std::this_thread::get_id(), "game/render/simulation did not use three threads");
    publish(frame);
    Require(presentation.PublishedGameplay()->Stamp == session.CurrentCommit(), "render did not consume the completed world");
}
} // namespace

int main() {
    try {
        WorkerLifetimeAndFailures();
        QueuedActionsAndCompletedFrames();
        BoundedQueue();
        BudgetRetryHoldsQueuedEffects();
        ThreeThreadsAndPlayerPresentation();
        std::cout << "Simulation worker, queued commands, player presentation and lifecycle passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
