#include "../Support/TestAccess.h"
#include "Runtime/Render/ActorFrame.h"
#include "Runtime/Render/GamePresentation.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
namespace process = formula::process;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
WorldPosition At(double x, double y) { return WorldPosition::FromAbsolute(x, y); }
ActorSnapshot Dummy(const Actors &actors) {
    std::optional<ActorSnapshot> result;
    for (const auto &actor : actors.Snapshot()) {
        if (actor.Kind != ActorKind::TrainingDummy) continue;
        Check(!result, "new world contains duplicate training dummies");
        result = actor;
    }
    Check(result.has_value(), "new world is missing its training dummy");
    return *result;
}
process::ProgramArtifact Program(std::string_view source) {
    auto result = process::Compile("training-dummy-test", source);
    if (const auto *error = std::get_if<process::Diagnostic>(&result)) throw std::runtime_error(error->Message);
    return std::get<process::ProgramArtifact>(std::move(result));
}

void DefaultSpawnAndPublication() {
    GameSession session(false);
    const auto target = Dummy(session.GetActors());
    const auto player = session.GetPlayer()->GetWorldPosition();
    Check(target.Position.RelativeTo(player.Cell()) == std::array<double, 2>{12, 0}, "dummy is not beside spawn");
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {480, 480, 64, 64});
    Check(view->GetActors().size() == 2, "complete commit omitted non-player actors");

    GameCamera camera;
    camera.SetFramebufferSize({640, 480});
    camera.Reset(session.GetWorld().GetSimulationBounds(), session.GetPlayer());
    GamePresentation presentation;
    auto publish = presentation.CreateFramePublisher(session, camera);
    WorldGridFrame frame;
    publish(frame);
    Check(frame.ActorDraws.size() == TrainingDummyDrawParts && frame.ActorDraws.front().ActorId == target.Id.Value,
          "default target was not published as a wooden dummy");
    ValidateWorldGridFrame(frame.Read(), 13, 640, 480);
    presentation.ToggleFieldView(); publish(frame);
    Check(frame.ActorDraws.empty(), "dummy polluted force diagnostic view");
    presentation.ToggleFieldView(); publish(frame);
    Check(frame.ActorDraws.size() == TrainingDummyDrawParts, "target disappeared after leaving diagnostic view");

    // A captured value cannot follow a live actor mutation until publication.
    auto &actors = testing::SessionAccess::Actors(session);
    actors.Remove(*actors.Resolve(target.Id));
    Check(view->GetActors().size() == 2, "actor snapshot aliases mutable live storage");
    testing::SessionAccess::PublishFixtureChanges(session);
    Check(view->GetActors().size() == 1, "actor removal was not published");
    Check(session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded(), "reset failed");
    Check(Dummy(session.GetActors()).Id != target.Id, "reset retained an old actor identity");
}

void NaturalAndFlatSpawn() {
    for (const auto kind : {WorldGeneratorKind::Natural, WorldGeneratorKind::Flat}) {
        WorldGenerationSettings generation;
        generation.Kind = kind; generation.Seed = 42;
        GameSession session(SessionMode::Temporary, generation);
        const auto target = Dummy(session.GetActors());
        const auto cell = target.Position.Cell();
        for (int y = 0; y < int(Body::Height); ++y)
            for (int x = 0; x < int(Body::Width); ++x)
                Check(session.GetWorld().GetCell(cell.X + x, cell.Y + y).Type == CellTypeId::Air,
                      "default target spawned inside terrain or water");
        for (int x = 0; x < int(Body::Width); ++x)
            Check(BlocksCharacter(session.GetWorld().GetCell(cell.X + x, cell.Y + 4).Type), "target has no spawn support");
        Check(session.Control(NativeCaller::Driver, api::CreateForceGarden{}).Succeeded(), "garden creation failed");
        Dummy(session.GetActors());
    }
}

void AnchoredAndRestored() {
    World world(64, 64);
    Actors actors;
    const auto handle = *actors.Spawn(ActorKind::TrainingDummy, At(20, 20));
    const auto id = actors.Require(handle).Id;
    const auto revision = actors.Revision();
    for (const auto material : {CellTypeId::Air, CellTypeId::Water, CellTypeId::Stone, CellTypeId::Fire}) {
        world.FillRegion({16, 16, 16, 16}, {material});
        for (int step = 0; step < 120; ++step) actors.Step(world.GetMap().Read(), {1, true});
        Check(actors.Require(handle).Body.GetWorldPosition().Cell() == CellCoord{20, 20}
              && actors.Revision() == revision, "target moved in response to input, gravity or material overlap");
    }
    Actors restored;
    restored.RestoreState(actors.CaptureState());
    restored.Step(world.GetMap().Read(), {-1, true});
    Check(restored.Find(id) && restored.Find(id)->Kind == ActorKind::TrainingDummy
          && restored.Find(id)->Body.GetWorldPosition().Cell() == CellCoord{20, 20}, "restore lost anchored target behavior");
}

void SweptHitsAndObstacles() {
    World world(64, 64);
    Actors actors;
    actors.Spawn(ActorKind::Player, At(15, 20));
    actors.Spawn(ActorKind::Resident, At(17, 20));
    const auto target = actors.Require(*actors.Spawn(ActorKind::TrainingDummy, At(20, 20))).Id;
    const auto values = actors.Snapshot();
    const auto sweep = [&](WorldPosition from, WorldPosition to) {
        return FormulaWorldAdapter::Sweep(world, from, to, .35, values);
    };
    const auto hit = sweep(At(10, 22), At(30, 22));
    Check(hit.Kind == FormulaContactKind::Actor && hit.Target == target && std::abs(hit.Fraction - .4825) < 1e-9,
          "fast bolt skipped target, hit the player/resident, or used an incorrect impact point");
    Check(sweep(At(21, 22), At(21, 22)).IsHit(), "bolt born inside target missed");
    Check(sweep(At(19.7, 19.7), At(19.7, 19.7)).Kind == FormulaContactKind::None,
          "expanded square incorrectly hit the rounded corner");
    Check(sweep(At(10, 19.65), At(30, 19.65)).IsHit(), "tangent bolt missed target");
    world.SetCell(18, 22, {CellTypeId::Stone});
    Check(sweep(At(10, 22), At(30, 22)).Kind == FormulaContactKind::Solid, "target behind terrain stole earlier wall contact");
    world.SetCell(18, 22, {});
    world.SetCell(25, 22, {CellTypeId::Stone});
    Check(sweep(At(10, 22), At(30, 22)).Kind == FormulaContactKind::Actor, "later wall stole earlier target contact");
}

void RepeatedHitCompositions() {
    World world(64, 64);
    Actors actors;
    const auto target = actors.Require(*actors.Spawn(ActorKind::TrainingDummy, At(20, 20))).Id;
    FormulaBridge bridge;
    bridge.BindActors(actors);
    bridge.ApplyProcess(Program("formula 2; run on_hit(bolt(1000000, speed(600), ticks(30)), dig(cells(2)));"));
    ProcessCarrier carrier;
    carrier.Kind = ProcessCarrierKind::Player; carrier.Subject = 1; carrier.Scope = {0, 0, 64, 64};
    for (int shot = 0; shot < 12; ++shot) {
        // Alternate swept contacts and contact at birth. The payload removes a
        // nearby ore cell, proving the hit reaches the real composition runtime.
        carrier.Origin = shot % 2 ? At(21, 22) : At(10, 22);
        world.SetCell(20, 23, {CellTypeId::CoalOre});
        bridge.BeginFrame();
        Check(bridge.CastCarrier(world, carrier), "target test cast refused");
        for (int step = 0; step < 8; ++step) { bridge.BeginFrame(); bridge.Step(world); }
        Check(world.GetCell(20, 23).Type == CellTypeId::Air && bridge.Stats().Roots == 0 && bridge.Projectiles().empty(),
              "target hit failed to invoke on_hit payload or retire projectile");
        Check(actors.Count() == 1 && actors.Find(target) && actors.Find(target)->Body.GetWorldPosition().Cell() == CellCoord{20, 20},
              "repeated high-damage hits killed or displaced target");
    }
}
} // namespace
int main() {
    try {
        DefaultSpawnAndPublication(); NaturalAndFlatSpawn(); AnchoredAndRestored(); SweptHitsAndObstacles(); RepeatedHitCompositions();
        std::cout << "Training dummy spawn, anchoring, composition hits and publication passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
