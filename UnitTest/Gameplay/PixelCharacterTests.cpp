#include "../Support/SimulatedWorld.h"
#include "PixelRender/PixelWorldPresentation.h"
#include <cmath>
#include <algorithm>
#include <iostream>
#include <stdexcept>

void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        pixel::testing::SimulatedWorld world(64, 64);
        world.GenerateTerrain(40);
        world.SpawnPlayer(20, 10);
        for (int i = 0; i < 120; ++i)
            world.Tick(1.f / 60);
        Require(world.GetPlayer()->Grounded && std::abs(world.GetPlayer()->Y - 36) < 0.001f, "falling player must land on dirt");
        Require(!world.PlaceDirt(20, 38), "editing must not put dirt inside the player");
        world.SetPlayerInput(1, false);
        world.Tick(0.25f);
        Require(std::abs(world.GetPlayer()->X - 24) < 0.001f, "horizontal walking distance");
        world.SetPlayerInput(0, true);
        world.Tick(1.f / 60);
        Require(world.GetPlayer()->Y < 36 && !world.GetPlayer()->Grounded, "ground jump");
        for (int i = 0; i < 120; ++i)
            world.Tick(1.f / 60);
        Require(world.GetPlayer()->Grounded, "jump must land without repeated input");
        for (uint32_t x = 23; x < 28; ++x)
            for (uint32_t y = 40; y < 48; ++y)
                world.RemoveDirt(x, y);
        for (int i = 0; i < 120; ++i)
            world.Tick(1.f / 60);
        Require(std::abs(world.GetPlayer()->Y - 44) < 0.001f, "removed support must cause falling");

        pixel::testing::SimulatedWorld thin(64, 64);
        thin.Fill({pixel::CellTypes::Empty, {0, 0}});
        for (uint32_t x = 0; x < 64; ++x)
            thin.SetCell(x, 40, {pixel::CellTypes::Dirt, {0, 0}});
        for (uint32_t y = 10; y < 40; ++y)
            thin.SetCell(30, y, {pixel::CellTypes::Dirt, {0, 0}});
        thin.SpawnPlayer(20, 5);
        for (int i = 0; i < 10; ++i)
            thin.Tick(0.25f);
        Require(std::abs(thin.GetPlayer()->Y - 36) < 0.001f, "must not tunnel through one-cell platform");
        thin.SetPlayerInput(1, false);
        for (int i = 0; i < 10; ++i)
            thin.Tick(0.25f);
        Require(std::abs(thin.GetPlayer()->X - 28) < 0.001f, "wall collision");
        thin.SetPlayerInput(-1, false);
        for (int i = 0; i < 20; ++i)
            thin.Tick(0.25f);
        Require(thin.GetPlayer()->X < 0.001f && thin.GetPlayer()->X >= 0, "world boundary");
        for (uint32_t x = 0; x < 4; ++x)
            thin.SetCell(x, 34, {pixel::CellTypes::Dirt, {0, 0}});
        thin.SetPlayerInput(0, true);
        thin.Tick(0.1f);
        Require(thin.GetPlayer()->Y >= 35, "jump must collide with thin ceiling");
        thin.SetPlayerInput(0, false);
        for (int i = 0; i < 60; ++i)
            thin.Tick(1.f / 60);
        Require(thin.GetPlayer()->Grounded, "must return to ground after ceiling contact");
        thin.SetCell(0, 39, {pixel::CellTypes::Dirt, {0, 0}});
        thin.Tick(1.f / 60);
        Require(thin.GetPlayer()->Y <= 35, "terrain entering body must resolve upward");
        const auto before = *thin.GetPlayer();
        thin.Tick(NAN);
        Require(thin.GetPlayer()->X == before.X && thin.GetPlayer()->Y == before.Y, "invalid duration");
        pixel::Character sprite;
        sprite.X = 511; sprite.Y = 508;
        WorldGridFrame frame;
        pixel::Camera2D camera;
        camera.SetPosition({512.125, 510.0625});
        sprite.X += 0.03125f;
        BuildCharacterFrame(sprite, camera, {1600, 1200}, frame);
        Require(frame.Character && frame.Character->Bounds == std::array<float, 4>{782.5f, 567.f, 32, 64},
                "character must retain fractional position and exact camera offset");
        Require(std::all_of(frame.Cells().begin(), frame.Cells().end(), [](uint32_t cell) { return cell == 0; }),
                "character must not be stamped into world cells");
        sprite.X = 100;
        camera.Translate({10, 10});
        Require(frame.Character->Bounds[0] == 782.5f, "published sprite must be independent of live character and camera");
        frame.DisplayMode = GridDisplayMode::ForceField;
        BuildCharacterFrame(sprite, camera, {1600, 1200}, frame);
        Require(!frame.Character, "field view must clear character snapshot");
        std::cout << "Character collision, movement and snapshot tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
