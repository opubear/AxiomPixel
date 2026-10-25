#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/Render/GamePresentation.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void SessionAndPublicationContracts() {
    pixel::GameSession session;
    auto &world = pixel::testing::SessionAccess::World(session);
    Require(session.IsPaused(), "session must start paused");
    Require(world.GetWindEndRow() == 424 && session.GetPlayer(), "default scene changed");
    Require(&world.GetBaseForceField() == &world.GetEnvironment().GetBaseForceField(),
            "environment must own the world base field");
    Require(session.GetSelectedWindStrategy() == pixel::WindStrategyKind::Empty &&
                world.GetWindStrategyKind() == pixel::WindStrategyKind::Empty,
            "session must start with empty wind");
    session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Coarse});
    Require(world.SampleWind(12.5f, 12.5f).X != 0, "comparison switch did not start wind");
    session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Empty});
    Require(world.SampleWind(12.5f, 12.5f) == pixel::WindVelocity{} && world.GetWindEndRow() == 424,
            "empty comparison must clear wind and preserve height");
    Require(session.IsPaused(), "switching wind must not resume simulation");
    world.SetBaseForce(12, 12, {123, 456});
    pixel::testing::SessionAccess::World(session).DisableWind();
    session.Execute(pixel::NativeCaller::Driver, pixel::api::SetWindBoundary{424});
    const auto force = world.GetBaseForceField().Get(12, 12);
    Require(force.X == 123 && force.Y == 456, "wind restart must preserve base forces");

    world.DisableWind();
    world.Fill({});
    world.PlaceDirt(32, 10);
    session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{true});
    session.Tick(.1f, {});
    Require(world.GetCell(32, 10).Type == pixel::CellTypes::Dirt && session.GetSimulationTime() == 0,
            "paused session must freeze simulation and wind animation");
    session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    session.Tick(std::numeric_limits<float>::quiet_NaN(), {});
    session.Tick(.2f, {});
    Require(world.GetCell(32, 11).Type == pixel::CellTypes::Dirt && session.IsPaused(),
            "single step must survive invalid duration and advance exactly once");
    const double time = session.GetSimulationTime();
    Require(std::abs(time - 1.0 / 60) < 1e-8, "single step must use fixed duration");
    session.Tick(.2f, {});
    Require(session.GetSimulationTime() == time && world.GetCell(32, 11).Type == pixel::CellTypes::Dirt,
            "single step must not remain latched");

    pixel::GameCamera camera;
    camera.SetFramebufferSize({800, 600});
    camera.Reset(world.GetSimulationBounds(), session.GetPlayer());
    const auto centre = camera.GetCamera().GetPosition();
    camera.Zoom(true);
    camera.SetFramebufferSize({1600, 1200});
    Require(camera.GetCamera().GetPosition() == centre, "zoom and resize must preserve world centre");
    // The old viewport centre must still select the camera's world centre after resize.
    const MousePointerState pointer{{400, 300}, {800, 600}, {800, 600}};
    const auto picked = camera.PickCell(pointer);
    Require(picked && (*picked)[0] == uint32_t(std::floor(centre[0])) && (*picked)[1] == uint32_t(std::floor(centre[1])),
            "queued click must use its captured dimensions");

    pixel::GamePresentation presentation;
    camera.GetCamera().SetPosition({32, 11});
    presentation.WindVisual.Mode = WindVisualMode::Off;
    auto publish = presentation.CreateFramePublisher(session, camera);
    auto otherPublish = presentation.CreateFramePublisher(session, camera);
    WorldGridFrame frame, other;
    publish(frame);
    const auto origin = frame.GetCellOrigin();
    const auto index = size_t(11 - origin[1]) * frame.GetWidth() + size_t(32 - origin[0]);
    Require(frame.Cells()[index] == 1 && frame.Character.has_value(), "publication must include materials and player");
    world.RemoveDirt(32, 11);
    otherPublish(other);
    Require(frame.Cells()[index] == 1 && other.Cells()[index] == 0,
            "publishers must not share mutable snapshots");
    presentation.ToggleFieldView();
    publish(frame);
    Require(frame.DisplayMode == GridDisplayMode::ForceField && !frame.Character,
            "field publication must clear character data");
    presentation.ToggleFieldView();
    pixel::testing::SessionAccess::Actors(session).Clear();
    world.GenerateTerrain(512);
    publish(frame);
    Require(frame.DisplayMode == GridDisplayMode::Materials && !frame.Character,
            "material publication must not retain removed character");

    session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
    session.Tick(1.f / 60, {});
    Require(session.GetSimulationTime() > time, "explicit resume must advance simulation");
    Require(session.Control(pixel::NativeCaller::Driver, pixel::api::ResetWorld{}).Succeeded(), "session control failed");
    Require(session.IsPaused() && session.GetSimulationTime() == 0 && world.GetWindEndRow() == 424 && session.GetPlayer(),
            "reset must restore scene and simulation controls");
    Require(world.GetWindStrategyKind() == pixel::WindStrategyKind::Empty, "reset must restore empty wind");
    const auto resetForce = world.GetBaseForceField().Get(12, 12);
    Require(resetForce.X == 0 && resetForce.Y == 3600, "terrain reset must restore base gravity");
    pixel::testing::SessionAccess::World(session).DisableWind();
    session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Reference});
    Require(session.IsPaused() && world.GetWindEndRow() == 0,
            "selecting expensive strategy while off must preserve disabled wind");
}
void FixedClockAndLatchedJump() {
    std::optional<pixel::WorldPosition> reference;
    for (const int rate : {30, 60, 120, 240}) {
        pixel::GameSession session;
        session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
        for (int frame = 0; frame < rate; ++frame)
            session.Tick(1.f / rate, {1, frame == 0});
        Require(std::abs(session.GetSimulationTime() - 1) < 1e-9,
                "session clock must count committed steps at all frame rates");
        const auto position = session.GetPlayer()->GetWorldPosition();
        if (reference) {
            Require(position.Chunk == reference->Chunk && std::abs(position.Local[0] - reference->Local[0]) < 1e-8
                        && std::abs(position.Local[1] - reference->Local[1]) < 1e-8,
                    "fixed character motion/jump depends on frame subdivision");
        } else {
            reference = position;
        }
    }

    pixel::GameSession session;
    pixel::testing::SessionAccess::RestoreClock(session, 0, .1, false);
    session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    session.Tick(.2f, {});
    Require(std::abs(session.GetSimulationTime() - 1.0 / 60) < 1e-10
                && std::abs(session.GetTimeRemainder() - (.1 - 1.0 / 60)) < 1e-10,
            "single step must consume one backlog step without adding or draining other time");
}
} // namespace
int main() {
    try {
        SessionAndPublicationContracts();
        FixedClockAndLatchedJump();
        std::cout << "Pixel session contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
