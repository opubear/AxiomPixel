#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Render/GamePresentation.h"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace pixel;
namespace {
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void CommitStampsAndPausedActions() {
    GameSession session;
    const auto initial = session.CurrentCommit();
    Require(initial.Tick == 0 && initial.Revision == 1 && !initial.WorldId.empty(), "initial commit missing");
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {504, 504, 32, 16});
    Require(view->Stamp() == initial, "read-only capture manufactured a commit");
    const api::ActionRequest request{session.WorldEpoch(), 1, api::MineRegion{{520, 512}}};
    const auto caller = session.CallerSession(NativeCaller::Driver);
    const auto mined = session.Submit(caller, request);
    Require(mined.Applied() && mined.ResolvedAt.Tick == 0 && mined.ResolvedAt.Revision == 2,
            "paused edit did not produce exactly one revision");
    Require(view->TryGetCell(520, 512)->Type == CellTypeId::Air && view->Stamp() == mined.ResolvedAt,
            "committed material view missed an action");
    const auto duplicate = session.Submit(caller, request);
    Require(duplicate.ResolvedAt == mined.ResolvedAt && session.CurrentCommit() == mined.ResolvedAt,
            "duplicate action changed the commit");
    const auto rejected = session.Execute(NativeCaller::Driver, api::MineRegion{{520, 512}});
    Require(!rejected.Applied() && rejected.ResolvedAt == mined.ResolvedAt,
            "rejected action changed the commit");
    const auto cast = session.Execute(NativeCaller::Driver,
        api::ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}});
    Require(cast.Applied() && cast.ResolvedAt.Tick == 0 && cast.ResolvedAt.Revision == 3,
            "paused activation masqueraded as a physical step");
    Require(session.Control(NativeCaller::Driver, api::SetPaused{true}).Succeeded(), "game pause failed");
    const auto frozen = session.CurrentCommit();
    const auto x = view->Projectiles().front().Position.Local[0];
    session.Tick(.1f, {});
    Require(session.CurrentCommit() == frozen && view->Projectiles().front().Position.Local[0] == x,
            "paused service frame advanced committed state");
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(.1f, {});
    Require(view->Stamp().Tick == 1 && view->Stamp().Revision == 5
                && std::abs(view->Projectiles().front().Position.Local[0] - x - 1) < 1e-8,
            "single step did not publish the complete formula stage");
    const auto stepped = session.CurrentCommit();
    Require(session.Submit(caller, request).ResolvedAt == mined.ResolvedAt,
            "cached terminal receipt was restamped at a later revision");
    session.Control(NativeCaller::Driver, api::SetPaused{true});
    Require(session.CurrentCommit() == stepped, "no-op pause manufactured a revision");
    session.Control(NativeCaller::Driver, api::SetPaused{false});
    Require(session.CurrentCommit().Tick == 1 && session.CurrentCommit().Revision == 6,
            "pause setting change lacks a revision");
    session.Control(NativeCaller::Driver, api::ResetWorld{});
    Require(session.CurrentCommit().WorldEpoch != initial.WorldEpoch && session.CurrentCommit().Revision == 1
                && session.CurrentCommit().Tick == 0 && view->Stamp() == session.CurrentCommit(),
            "world replacement retained an old epoch projection");
}

struct TemporaryWorld {
    std::filesystem::path Directory = std::filesystem::temp_directory_path()
        / ("pixel-commit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryWorld() { std::filesystem::remove_all(Directory); }
};

void PendingStepRetainsCompleteProjection() {
    TemporaryWorld files;
    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    WorldStreamSettings settings;
    settings.Directory = files.Directory;
    settings.WindowEdge = 256;
    settings.SurfaceY = 10000;
    settings.BudgetBytes = 50ull * 1024 * 1024;
    world.EnableStreaming(settings);
    testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(191.9, 128));
    WindStrategySettings wind;
    wind.Kind = WindStrategyKind::Procedural;
    world.ConfigureWindDomain(256, wind);
    world.SetCell(32, 10, {CellTypeId::Dirt, {0, 1}});
    world.SetCell(32, 9, {CellTypeId::Fire});
    world.SetCell(33, 9, {CellTypeId::Water});
    world.SetCell(30, 10, {CellTypeId::Steam});
    session.Control(NativeCaller::Driver, api::SetPaused{false});
    Require(session.Execute(NativeCaller::Driver,
        api::ActivateProgram{session.AppliedProgram(), {{240, 130}, {0, 0}}}).Applied(), "pending fixture cast failed");
    GameCamera camera;
    camera.SetFramebufferSize({256, 256});
    camera.GetCamera().SetPosition({100, 100});
    GamePresentation presentation;
    presentation.DisplayMode = GridDisplayMode::ForceField;
    auto publish = presentation.CreateFramePublisher(session, camera);
    WorldGridFrame frame;
    publish(frame);
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {64, 64, 64, 64});
    world.WaitForStreaming();
    for (const auto coordinate : world.GetMap().ChunkCoordinates())
        if (!world.GetSimulationBounds().Contains({coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge}))
            testing::WorldAccess::Map(world).Remove(coordinate);
    testing::SessionAccess::PublishFixtureChanges(session);
    const auto stamp = session.CurrentCommit();
    const auto sample = view->SampleWind({100, 100});
    const auto player = view->GetPlayer()->GetWorldPosition();
    const auto bolt = view->Projectiles().front().Position;
    const std::vector<uint32_t> colors(frame.Cells().begin(), frame.Cells().end());
    testing::WorldAccess::SaturateBudget(world);
    session.Tick(.1f, {1, false});
    Require(world.GetEnvironment().IsWaitingForBudget(), "fixture did not exhaust scratch budget");
    Require(world.GetCell(32, 9).Type == CellTypeId::Fire && world.GetCell(30, 10).Type == CellTypeId::Steam,
            "failed material step partially committed decay or motion");
    Require(session.CurrentCommit() == stamp && view->Stamp() == stamp && view->SampleWind({100, 100}) == sample,
            "prepared wind leaked into the committed view");
    Require(world.SampleWind(CellCoord{100, 100}) != sample, "fixture must actually change prepared wind");
    const auto preparedRevision = world.GetEnvironment().GetWindSimulation().GetRevision();
    const auto bounds = world.GetSimulationBounds();
    const auto chunkCount = world.GetMap().ChunkCount();
    const auto pendingRemainder = session.GetTimeRemainder();
    session.ClearPlayerInput();
    session.Tick(.2f, {-1, true});
    publish(frame);
    Require(session.CurrentCommit() == stamp && view->Stamp() == stamp
                && std::equal(colors.begin(), colors.end(), frame.Cells().begin())
                && world.GetEnvironment().GetWindSimulation().GetRevision() == preparedRevision
                && world.GetSimulationBounds() == bounds && world.GetMap().ChunkCount() == chunkCount
                && session.GetTimeRemainder() == pendingRemainder,
            "retry advanced wind, admitted residency, changed the domain, or published a half step");
    Require(view->GetPlayer()->GetWorldPosition().Local == player.Local
                && view->Projectiles().front().Position.Local == bolt.Local, "waiting advanced actors or formulas");
    Require(session.Execute(NativeCaller::Driver, api::MineRegion{{32, 10}}).Rejection == api::Reason::WaitingForBudget,
            "pending step admitted a material edit");
    Require(session.Control(NativeCaller::Driver, api::SetPaused{true}).Rejection == api::Reason::WaitingForBudget,
            "pending step published a pause change");
    auto unavailable = session.CreateReadView();
    session.RefreshReadView(*unavailable, {64, 64, 64, 64});
    Require(!unavailable->IsValid(), "new consumer sampled a prepared solver");
    GameCamera distantCamera;
    distantCamera.SetFramebufferSize({256, 256});
    distantCamera.GetCamera().SetWorldPosition({{int64_t{1} << 40, -(int64_t{1} << 40)}, {.5, .5}});
    auto missingPublisher = presentation.CreateFramePublisher(session, distantCamera);
    WorldGridFrame missingFrame;
    missingPublisher(missingFrame);
    ValidateWorldGridFrame(missingFrame.Read(), 11, 256, 256);
    const auto missingVersion = missingFrame.Read().RegionVersions[0];
    std::fill(missingFrame.Cells().begin(), missingFrame.Cells().end(), 1);
    missingPublisher(missingFrame);
    Require(missingFrame.Read().RegionVersions[0] != missingVersion
                && std::all_of(missingFrame.Cells().begin(), missingFrame.Cells().end(),
                               [](auto cell) { return cell == Grid2D::MissingCell; })
                && !missingFrame.Character && !missingFrame.HasWind && missingFrame.FormulaDraws.empty(),
            "missing projection retained old GPU versions or live presentation");
    world.SetPublicationBytes(0);
    session.Control(NativeCaller::Driver, api::StepSimulation{});
    session.Tick(.2f, {-1, true});
    Require(session.CurrentCommit().Tick == stamp.Tick + 1 && session.CurrentCommit().Revision == stamp.Revision + 1
                && view->Stamp() == session.CurrentCommit()
                && std::abs(session.GetTimeRemainder() - (pendingRemainder - 1.0 / 60)) < 1e-10,
            "budget recovery did not commit exactly one complete step with retained backlog");
    // The frozen input moves x=191.9 across a 64-cell chunk boundary.
    // Compare in one coordinate frame rather than comparing wrapped locals.
    const auto beforePosition = player.RelativeTo(player.Cell());
    const auto afterPosition = session.GetPlayer()->GetWorldPosition().RelativeTo(player.Cell());
    Require(afterPosition[0] > beforePosition[0] && afterPosition[1] >= beforePosition[1],
            "input or clear during waiting replaced frozen movement/jump intent");
    Require(std::abs(view->Projectiles().front().Position.Local[0] - bolt.Local[0] - 1) < 1e-8,
            "formula did not advance exactly once after budget recovery");
}
} // namespace

int main() {
    try {
        CommitStampsAndPausedActions();
        PendingStepRetainsCompleteProjection();
        std::cout << "Simulation commit contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
