#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/System/IO/Input/GameInput.h"
#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Render/GamePresentation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace pixel;
using namespace pixel::api;
formula::process::ProgramArtifact Program(std::string_view source) {
    return std::get<formula::process::ProgramArtifact>(formula::process::Compile("test.process", source));
}

void Require(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
void ThreeEntrances() {
    for (const auto caller : {NativeCaller::Input, NativeCaller::Debug, NativeCaller::Driver}) {
        GameSession session;
        GameCamera camera;
        GamePresentation presentation;
        GameInput input(session, camera, presentation, {[] {}, [] {}, [] {}, [] { return std::array{800, 600}; }});
        camera.GetCamera().SetPosition({520, 512});
        const MousePointerState pointer{{400, 300}, {800, 600}, {800, 600}};
        if (caller == NativeCaller::Input)
            input.OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_PRESS, int(Mod::MOD_SHIFT), pointer);
        else session.Execute(caller, MineRegion{{520, 512}, uint32_t(CellTypeId::Dirt)});
        const auto &receipt = *session.LastAction(caller);
        const auto result = std::get<MaterialResult>(receipt.Result);
        Require(receipt.Applied() && result.Changed == 1 && result.Before == 10 && result.After == 0,
            "input/debug/driver mining must return identical domain results");
        Require(!session.IsPaused() && session.GetSimulationTime() == 0, "dispatching an action advanced the clock");
        Require(session.Execute(caller, PlaceMaterial{{511, 508}}).Rejection == Reason::Blocked,
            "placement inside the player must fail");
        pixel::testing::SessionAccess::World(session).SetCell(20, 20, {CellTypeId::Water, {0, 1}});
        Require(session.Execute(caller, PlaceMaterial{{20, 20}}).Rejection == Reason::Blocked,
            "placement must not overwrite water");
        const auto missing = session.Execute(caller, MineRegion{{-1, -1}, 10});
        Require(missing.Rejection == Reason::NotReady && std::get<MaterialResult>(missing.Result).NotReady == 1,
            "missing cells must win over expected-material checks");
        Require(session.Execute(caller, MineRegion{{520, 512}, 10}).Rejection == Reason::PreconditionFailed,
            "stale material observation was accepted");
        Require(session.Execute(caller, MineRegion{{520, 512}, {}}).Rejection == Reason::Blocked,
            "mining air must be blocked");
    }
}
void RequestLifetimeAndValidation() {
    GameSession session;
    const auto caller = session.CallerSession(NativeCaller::Driver);
    const ActionRequest request{session.WorldEpoch(), 1, MineRegion{{520, 512}, 10}};
    const auto first = session.Submit(caller, request);
    Require(first.Applied(), "first request failed");
    pixel::testing::SessionAccess::World(session).PlaceDirt(520, 512);
    const auto duplicate = session.Submit(caller, request);
    Require(duplicate.Key == first.Key && duplicate.Applied() && pixel::testing::SessionAccess::World(session).GetCell(520, 512).Type == CellTypeId::Dirt,
            "duplicate request executed twice");
    for (size_t attempt = 0; attempt < 64; ++attempt)
        Require(session.Submit(caller, {session.WorldEpoch(), 0, PlaceMaterial{{1, 1}}}).Rejection == Reason::InvalidArgument,
                "zero sequence accepted");
    Require(session.Submit(caller, request).Applied(), "invalid zero sequences evicted a valid terminal receipt");
    for (uint64_t sequence = 2; sequence <= 65; ++sequence)
        session.Submit(caller, {session.WorldEpoch(), sequence, PlaceMaterial{{511, 508}}});
    Require(session.Submit(caller, request).Rejection == Reason::DuplicateExpired, "expired request executed again");
    const auto invalid = session.Execute(NativeCaller::Driver,
        ActivateProgram{session.AppliedProgram(), {{512, 510}, {std::numeric_limits<double>::quiet_NaN(), 0}}});
    Require(invalid.Rejection == Reason::InvalidArgument, "NaN position accepted");
    Require(session.Execute(NativeCaller::Driver, PlaceMaterial{{0, 0}, 50}).Rejection == Reason::Unsupported,
            "unsupported placement accepted");
    Require(session.Execute(NativeCaller::Driver, MineRegion{{INT64_MAX, INT64_MIN}, {}}).Rejection == Reason::InvalidArgument,
            "unrepresentable coordinates accepted");
    const auto player = session.CallerSession(NativeCaller::Input);
    Require(session.Submit(player, {session.WorldEpoch(), 1, SetWindBoundary{10}}).Rejection == Reason::PermissionDenied,
            "input caller acquired debug wind capability");
    Require(session.Submit(player, {session.WorldEpoch(), 2, SelectWindStrategy{WindKind(999)}}).Rejection == Reason::Unsupported,
            "payload validation must precede capability checks");
    Require(session.Submit(caller, {session.WorldEpoch() + 1, 999, PlaceMaterial{{1, 1}}}).Rejection == Reason::StaleEpoch,
            "wrong epoch accepted");
    GameSession other;
    Require(other.WorldEpoch() != session.WorldEpoch() && other.Submit(caller, request).Rejection == Reason::CallerExpired,
            "caller or epoch leaked across sessions");
    Require(session.Control(NativeCaller::Input, ResetWorld{}).Status == ControlStatus::Replaced, "reset control failed");
    Require(session.Submit(caller, request).Rejection == Reason::CallerExpired, "reset did not expire callers");
    Require(session.Execute(NativeCaller::Input, MineRegion{{520, 512}, {}}).Applied(), "new bag could not collect soil");
    Require(session.Execute(NativeCaller::Input, PlaceMaterial{{1, 1}}).Applied(), "native caller did not renew after reset");
}
void ProgramAndControls() {
    GameSession session;
    const auto oldProgram = session.AppliedProgram();
    const auto invalid = formula::process::Compile("invalid", "process 1; wait ticks(false);");
    Require(std::holds_alternative<formula::process::Diagnostic>(invalid) && session.AppliedProgram() == oldProgram,
            "invalid user program must return diagnostics and preserve the applied version");
    const auto cast = session.Execute(NativeCaller::Input, ActivateProgram{oldProgram, {{600, 510}, {0, 0}}});
    Require(cast.Applied() && std::get<ActivationResult>(cast.Result).ActivationId != 0, "activation identity missing");
    const auto original = session.GetFormulaRuntime().Projectiles().front();
    const auto position = original.Position.RelativeTo({513, 510});
    Require(std::abs(position[0] - .6) < 1e-9 && std::abs(position[1]) < 1e-9, "muzzle geometry changed");
    session.Control(NativeCaller::Driver, SetPaused{true});
    session.Tick(.1f, {});
    Require(session.GetFormulaRuntime().Projectiles().front().Position.Local == original.Position.Local, "pause moved a ball");
    session.Control(NativeCaller::Driver, SetPaused{false});
    const auto applied = session.ApplyProcess(NativeCaller::Debug, Program("process 1;"));
    Require(bool(applied) && applied.Program != oldProgram, "apply did not publish a new version");
    Require(session.Execute(NativeCaller::Input, ActivateProgram{oldProgram, {{600, 510}, {0, 0}}}).Rejection == Reason::StaleProgram,
            "old program version accepted");
    Require(session.Execute(NativeCaller::Debug, ActivateProgram{*applied.Program, {{600, 510}, {0, 0}}}).Applied(),
            "Empty activation must succeed without a ball");
    session.Control(NativeCaller::Debug, StepSimulation{});
    session.Tick(.1f, {});
    const auto &ball = session.GetFormulaRuntime().Projectiles().front();
    Require(ball.Version == original.Version && std::abs(ball.Position.RelativeTo(original.Position.Cell())[0] - 1.6) < 1e-8,
            "apply changed old instances or single-step timing");
    Require(session.Control(NativeCaller::Debug, ClearRunning{}).Succeeded() && session.GetFormulaRuntime().Stats().Roots == 0,
            "clear left running roots");
    session.Control(NativeCaller::Debug, SetNextGenerator{GeneratorKind::Natural});
    session.Control(NativeCaller::Debug, SetNextSeed{123});
    Require(session.GetNextGenerationSettings().Seed == 123 && session.GetNextGenerationSettings().Kind == WorldGeneratorKind::Natural,
            "generation controls failed");
    Require(session.Control(NativeCaller::Debug, SaveWorld{}).Status == ControlStatus::Saved, "finite save control failed");
    session.Control(NativeCaller::Debug, SetPaused{false});
    Require(!session.IsPaused(), "pause control failed");
    Require(session.Execute(NativeCaller::Debug, SelectWindStrategy{WindKind::Empty}).Applied(), "wind control failed");
}
void WaitingAndFarCoordinates() {
    GameSession session;
    auto &world = pixel::testing::SessionAccess::World(session);
    pixel::testing::SessionAccess::RestoreClock(session, 0, 1.0 / 60, true);
    for (const auto caller : {NativeCaller::Input, NativeCaller::Debug, NativeCaller::Driver}) {
        Require(session.Execute(caller, MineRegion{{520, 512}, {}}).Rejection == Reason::WaitingForBudget,
            "waiting did not refuse terrain edits");
        Require(session.Execute(caller, ActivateProgram{session.AppliedProgram(), {{600, 510}, {0, 0}}}).Rejection == Reason::WaitingForBudget,
            "waiting did not refuse activation");
    }
    Require(session.ApplyProcess(NativeCaller::Debug, Program("process 1;")).Rejection == Reason::WaitingForBudget,
            "waiting did not refuse program mutation");
    Require(session.Control(NativeCaller::Debug, ClearRunning{}).Rejection == Reason::WaitingForBudget,
            "waiting did not refuse clearing running state");
    pixel::testing::SessionAccess::RestoreClock(session, 0, 0);
    const CellCoord far{(int64_t{1} << 45) + 17, -(int64_t{1} << 45) - 17};
    WorldPosition point{ChunkOf(far), {17.125, 33.875}};
    point.Normalize();
    const auto dto = ActionPosition(point);
    Require(dto.AnchorCell == Cell{point.Cell().X, point.Cell().Y} && dto.Offset == std::array{.125, .875},
            "position DTO lost integer or fractional precision");
    Require(session.Execute(NativeCaller::Driver, ActivateProgram{session.AppliedProgram(), dto}).Applied(),
            "valid distant target rejected");
}
void ElementBrushActions() {
    GameSession session;
    const SpawnElement brush{{100, 100}, uint32_t(CellTypeId::Steam), 2};
    Require(session.Execute(NativeCaller::Input, brush).Rejection == Reason::PermissionDenied,
            "normal gameplay acquired the debug element brush");
    auto &world = testing::SessionAccess::World(session);
    world.SetCell(100, 100, {CellTypeId::Stone});
    world.SetBaseForce(101, 100, {123, 456});
    const auto before = session.CurrentCommit();
    const ActionRequest request{session.WorldEpoch(), 10, brush};
    const auto receipt = session.Submit(session.CallerSession(NativeCaller::Debug), request);
    const auto result = std::get<ElementsSpawned>(receipt.Result);
    Require(receipt.Applied() && result.Changed == 12 && result.Radius == 2,
            "element brush failed to fill just the empty cells in a bounded disk");
    Require(world.GetCell(100, 100).Type == CellTypeId::Stone
                && world.GetCell(101, 100).Type == CellTypeId::Steam
                && world.GetBaseForceField().Get(101, 100) == ForceVector{123, 456},
            "brush overwrote terrain or spatial forces");
    Require(session.CurrentCommit().Tick == before.Tick && session.CurrentCommit().Revision == before.Revision + 1,
            "brush did not publish exactly one non-physics commit");
    const auto facts = session.ReadFacts(session.WorldEpoch(), receipt.Facts.FirstSequence - 1,
                                          receipt.Facts.FirstSequence, 1);
    Require(facts.Facts.size() == 1 && std::get<ElementsSpawned>(facts.Facts.front().Payload).Changed == 12,
            "element brush lost its committed fact");
    const auto duplicate = session.Submit(session.CallerSession(NativeCaller::Debug), request);
    Require(duplicate.ResolvedAt == receipt.ResolvedAt && session.CurrentCommit() == receipt.ResolvedAt,
            "duplicate brush request ran twice");
    Require(session.Execute(NativeCaller::Debug, SpawnElement{{100, 100}, 999, 2}).Rejection == Reason::Unsupported,
            "brush accepted an unregistered material");
    Require(session.Execute(NativeCaller::Debug, SpawnElement{{100, 100}, 50, 9}).Rejection == Reason::InvalidArgument,
            "brush accepted an unbounded disk");
    session.Control(NativeCaller::Debug, SetPaused{true});
    Require(session.Execute(NativeCaller::Debug, SpawnElement{{110, 100}, 52, 0}).Rejection == Reason::GamePaused,
            "paused game accepted element creation");
    const auto cells = world.ReadState().AtLocal(101, 100);
    session.Tick(.2f, {});
    Require(world.ReadState().AtLocal(101, 100) == cells, "paused game simulated steam");
}

} // namespace
int main() {
    try {
        ElementBrushActions(); ThreeEntrances(); RequestLifetimeAndValidation(); ProgramAndControls(); WaitingAndFarCoordinates();
        std::cout << "Pixel actions passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
