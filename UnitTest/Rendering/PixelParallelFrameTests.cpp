#include "Core/Threading/FramePipeline.h"
#include "Runtime/Gameplay/UI/InventoryUI.h"
#include "Runtime/Render/GamePresentation.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <vector>

namespace {
void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void Await(std::binary_semaphore &signal) {
    Require(signal.try_acquire_for(std::chrono::seconds(5)), "Pixel game and rendering did not overlap");
}

void FrozenPresentationDuringSimulation() {
    pixel::GameSession session;
    pixel::GameCamera camera;
    camera.SetFramebufferSize({256, 256});
    camera.Reset(session.GetWorld().GetSimulationBounds(), session.GetPlayer());
    pixel::GamePresentation presentation;
    pixel::ui::InventoryUI ui;
    auto publish = presentation.CreateFramePublisher(session, camera);
    WorldGridFrame frame;
    session.Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});

    std::binary_semaphore started{0}, rendering{0};
    axiom::FramePipeline pipeline([&](float duration) {
        started.release();
        Await(rendering);
        session.Tick(duration, {1, false});
        camera.FollowPlayer(session.GetPlayer(), duration);
    });
    for (unsigned index = 0; index < 16; ++index) {
        session.BeginFrame();
        publish(frame);
        Require(presentation.PublishedGameplay().has_value(), "initial game snapshot was not published");
        const auto stamp = presentation.PublishedGameplay()->Stamp;
        ui.Publish(&*presentation.PublishedGameplay(), session.PlayerInventory());
        const auto canvas = ui.Paint();
        const auto origin = frame.View.OriginPixels;
        const auto worldOrigin = frame.View.WorldOriginCells;
        const auto character = frame.Character;
        const std::vector<uint32_t> cells(frame.Read().Cells.begin(), frame.Read().Cells.end());
        const std::vector<uint32_t> wind(frame.Read().WindCells.begin(), frame.Read().WindCells.end());
        const std::vector<uint64_t> versions(frame.Read().RegionVersions.begin(), frame.Read().RegionVersions.end());
        pipeline.Run(1.0f / 60, [&] {
            Await(started);
            rendering.release();
            // Re-read render-owned arrays while real simulation updates its
            // committed read views, player, world buffers and camera.
            for (unsigned read = 0; read < 64; ++read) {
                const auto frozen = frame.Read();
                Require(std::equal(cells.begin(), cells.end(), frozen.Cells.begin())
                            && std::equal(wind.begin(), wind.end(), frozen.WindCells.begin())
                            && std::equal(versions.begin(), versions.end(), frozen.RegionVersions.begin()),
                        "simulation changed published terrain, wind or upload versions");
                Require(frozen.View.OriginPixels == origin && frozen.View.WorldOriginCells == worldOrigin
                            && frozen.Character.has_value() == character.has_value()
                            && (!character || frozen.Character->Bounds == character->Bounds),
                        "simulation changed the published camera or character");
                Require(presentation.PublishedGameplay()->Stamp == stamp && ui.Paint().Pixels == canvas.Pixels,
                        "simulation changed published gameplay or inventory UI");
            }
        });
        Require(session.CurrentCommit().Tick > stamp.Tick, "game did not advance beyond the rendered commit");
        Require(presentation.PublishedGameplay()->Stamp == stamp, "game completion replaced a frame before publication");
    }
    const auto previous = presentation.PublishedGameplay()->Stamp;
    Require(session.Control(pixel::NativeCaller::Driver, pixel::api::ResetWorld{}).Succeeded(), "reset failed");
    Require(presentation.PublishedGameplay()->Stamp == previous, "reset invalidated the retained render frame");
    publish(frame);
    Require(presentation.PublishedGameplay()->Stamp.WorldEpoch != previous.WorldEpoch,
            "next publication did not switch to the reset world");
}
} // namespace

int main() {
    try {
        FrozenPresentationDuringSimulation();
        std::cout << "Pixel parallel simulation and frozen render publication passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
