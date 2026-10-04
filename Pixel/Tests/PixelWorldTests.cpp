#include "PixelRender/PixelWorldPresentation.h"
#include "World/World.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class Error, class F>
void Reject(F &&action) {
    try {
        action();
    } catch (const Error &) {
        return;
    }
    throw std::runtime_error("expected world input rejection");
}
void WorldOperations() {
    Reject<std::invalid_argument>([] { pixel::World world(0, 4); });
    Reject<std::invalid_argument>([] { pixel::World world(4, 0); });
    Reject<std::invalid_argument>([] { pixel::WorldState state(UINT32_MAX, UINT32_MAX); });
    pixel::World world(3, 2);
    Check(world.ReadState().Cells.size() == 6, "incorrect world extent");
    for (auto cell : world.ReadState().Cells)
        Check(cell.Type.Value == 0, "world not initially empty");
    world.SetCell(2, 1, {{10}});
    Check(world.GetCell(2, 1).Type.Value == 10 && world.ReadState().Cells[5].Type.Value == 10, "world indexing incorrect");
    Reject<std::out_of_range>([&] { world.SetCell(3, 0, {{20}}); });
    Reject<std::out_of_range>([&] { world.GetCell(0, 2); });
    Reject<std::out_of_range>([&] { world.GetCell(UINT32_MAX, 0); });
    world.Fill({{20}});
    world.Tick(0.016f);
    for (auto cell : world.ReadState().Cells)
        Check(cell.Type.Value == 20, "fill or empty character/environment tick changed world");
    world.Fill({});
    Check(world.GetCell(2, 1).Type.Value == 0, "world clearing failed");
}
void WorldPublication() {
    pixel::World world(3, 2);
    pixel::WorldState snapshot(3, 2);
    const std::array storage{world.ReadState().Cells.data(), snapshot.Read().Cells.data()};
    world.SetCell(0, 0, {{10}});
    for (size_t frame = 0; frame < 8; ++frame) {
        world.PublishState(snapshot);
        Check(world.ReadState().Cells.data() == storage[(frame + 1) % 2], "world allocated instead of recycling storage");
        Check(snapshot.Read().Cells.data() == storage[frame % 2], "snapshot did not receive the working storage");
        Check(world.GetCell(0, 0).Type.Value == 10 && snapshot.Read().Cells[0].Type.Value == 10, "persistent edit lost across frames");
        const auto before = snapshot.Read().Cells[5];
        world.SetCell(2, 1, {{uint32_t(20 + frame)}});
        Check(snapshot.Read().Cells[5] == before, "working edit changed the published snapshot");
        if (frame > 0)
            Check(before.Type.Value == 19 + frame, "second edit did not persist until publication");
    }
    world.SetCell(0, 0, {});
    world.PublishState(snapshot);
    world.PublishState(snapshot); // An unchanged frame must not resurrect old values.
    Check(snapshot.Read().Cells[0].Type.Value == 0 && snapshot.Read().Cells[5].Type.Value == 27, "clear or unchanged frame reverted data");
    pixel::WorldState wrongShape(2, 3);
    const auto before = world.ReadState().Cells.data();
    Reject<std::invalid_argument>([&] { world.PublishState(wrongShape); });
    Check(world.ReadState().Cells.data() == before && world.GetCell(2, 1).Type.Value == 27, "rejected publication modified world");
}
void PresentationMapping() {
    pixel::World world(Grid2D::Width, Grid2D::Height);
    pixel::WorldState snapshot(Grid2D::Width, Grid2D::Height);
    WorldGridFrame output;
    const auto *renderStorage = output.Cells().data();
    CellAppearanceTable appearances{{10, 2}, {20, 2}};
    world.SetCell(0, 0, {{10}});
    world.SetCell(Grid2D::Width - 1, Grid2D::Height - 1, {{20}});
    world.PublishState(snapshot);
    BuildWorldGridFrame(snapshot.Read(), appearances, output);
    Check(output.Read().Cells.front() == 2 && output.Read().Cells.back() == 2 && output.Read().Cells[1] == 0, "nonidentity or shared appearance mapping failed");
    appearances[10] = 3;
    BuildWorldGridFrame(snapshot.Read(), appearances, output);
    Check(output.Read().Cells.front() == 3 && output.Cells().data() == renderStorage, "mapping change did not reuse render storage");
    Check(world.GetCell(0, 0).Type.Value == 10 && snapshot.Read().Cells[0].Type.Value == 10, "presentation changed gameplay identity");
    world.SetCell(0, 0, {{99}});
    BuildWorldGridFrame(snapshot.Read(), appearances, output);
    Check(output.Read().Cells[0] == 3, "unpublished gameplay leaked into conversion");
    world.PublishState(snapshot);
    Reject<std::invalid_argument>([&] { BuildWorldGridFrame(snapshot.Read(), appearances, output); });
    appearances[99] = 4;
    BuildWorldGridFrame(snapshot.Read(), appearances, output);
    Reject<std::invalid_argument>([&] { ValidateWorldGridFrame(output.Read(), 4, 800, 600); });
    world.Fill({});
    world.PublishState(snapshot);
    appearances[0] = 3; // Even an explicit appearance cannot turn empty space into a material.
    BuildWorldGridFrame(snapshot.Read(), appearances, output);
    Check(std::all_of(output.Read().Cells.begin(), output.Read().Cells.end(), [](uint32_t id) { return id == 0; }), "empty gameplay cells must map to zero");
    auto malformed = snapshot.Read();
    malformed.Width = 2;
    Reject<std::invalid_argument>([&] { BuildWorldGridFrame(malformed, appearances, output); });
    malformed = snapshot.Read();
    malformed.Cells = malformed.Cells.first(1);
    Reject<std::invalid_argument>([&] { BuildWorldGridFrame(malformed, appearances, output); });
}
void TerrainAndGravity() {
    constexpr pixel::CellState dirt{pixel::CellTypes::Dirt};
    pixel::World terrain(4, 1024);
    terrain.GenerateTerrain(512);
    for (uint32_t y = 0; y < 1024; ++y)
        for (uint32_t x = 0; x < 4; ++x)
            Check(terrain.GetCell(x, y) == (y < 512 ? pixel::CellState{} : dirt), "terrain boundary is not row 512");
    Check(terrain.PlaceDirt(1, 508) && !terrain.PlaceDirt(1, 508), "placement must affect one empty cell only");
    terrain.Tick(1.f / 60);
    Check(terrain.GetCell(1, 508).Type == pixel::CellTypes::Empty && terrain.GetCell(1, 509) == dirt, "dirt did not fall exactly one row");
    for (int i = 0; i < 10; ++i)
        terrain.Tick(1.f / 60);
    Check(terrain.GetCell(1, 511) == dirt && terrain.GetCell(1, 510).Type == pixel::CellTypes::Empty, "dirt did not stop above terrain");
    Check(terrain.RemoveDirt(1, 513) && !terrain.RemoveDirt(1, 513), "removal must affect dirt only");
    terrain.Tick(1.f / 60);
    Check(terrain.GetCell(1, 511).Type == pixel::CellTypes::Empty && terrain.GetCell(1, 512) == dirt && terrain.GetCell(1, 513) == dirt, "unsupported stack did not fall into the excavated cell");
    Check(terrain.GetCell(0, 511).Type == pixel::CellTypes::Empty && terrain.GetCell(0, 512) == dirt, "gravity moved a neighbouring column");
    terrain.GenerateTerrain(512);
    Reject<std::out_of_range>([&] { terrain.GenerateTerrain(1025); });
    Check(terrain.GetCell(1, 511).Type == pixel::CellTypes::Empty && terrain.GetCell(1, 512) == dirt, "reset did not restore terrain");

    pixel::World world(3, 6);
    world.PlaceDirt(0, 0);
    world.PlaceDirt(0, 1);
    world.PlaceDirt(2, 5);
    world.SetCell(1, 3, {{20}});
    Check(!world.RemoveDirt(1, 3) && !world.PlaceDirt(1, 3), "dirt editing overwrote another material");
    pixel::WorldState snapshot(3, 6);
    world.PublishState(snapshot);
    for (int i = 0; i < 20; ++i)
        world.Tick(1.f / 60);
    Check(world.GetCell(0, 4) == dirt && world.GetCell(0, 5) == dirt && world.GetCell(2, 5) == dirt, "stack escaped the bottom or failed to settle");
    Check(std::count(world.ReadState().Cells.begin(), world.ReadState().Cells.end(), dirt) == 3, "gravity lost or duplicated dirt");
    Check(snapshot.Read().Cells[0] == dirt && snapshot.Read().Cells[3] == dirt, "simulation mutated a published snapshot");
    world.PublishState(snapshot);
    Check(snapshot.Read().Cells[12] == dirt && snapshot.Read().Cells[15] == dirt, "publication lost settled blocks");
    pixel::World singleRow(1, 1);
    singleRow.PlaceDirt(0, 0);
    singleRow.Tick(1);
    Check(singleRow.GetCell(0, 0) == dirt, "single-row world lost its bottom block");
}
void GravityTiming() {
    pixel::World fast(1, 100), slow(1, 100);
    fast.PlaceDirt(0, 0);
    slow.PlaceDirt(0, 0);
    fast.Tick(0);
    fast.Tick(-1);
    fast.Tick(std::numeric_limits<float>::quiet_NaN());
    fast.Tick(std::numeric_limits<float>::infinity());
    Check(fast.GetCell(0, 0).Type == pixel::CellTypes::Dirt, "invalid time advanced gravity");
    for (int i = 0; i < 60; ++i)
        fast.Tick(1.f / 120);
    for (int i = 0; i < 15; ++i)
        slow.Tick(1.f / 30);
    Check(std::equal(fast.ReadState().Cells.begin(), fast.ReadState().Cells.end(), slow.ReadState().Cells.begin()), "gravity depends on frame rate");
    Check(fast.GetCell(0, 30).Type == pixel::CellTypes::Dirt, "gravity does not move at 60 cells per second");
    fast.GenerateTerrain(100);
    fast.PlaceDirt(0, 0);
    fast.Tick(1.f / 120);
    Check(fast.GetCell(0, 0).Type == pixel::CellTypes::Dirt, "reset retained old simulation time");
    fast.Tick(10);
    Check(fast.GetCell(0, 15).Type == pixel::CellTypes::Dirt, "long pause did not bound gravity catch-up");
}
void MousePicking() {
    GridView2D view;
    view.OriginPixels = {32, 512 * 16 - 32};
    Check(PickGridCell(view, {8, 16}, {800, 600}, {1600, 1200}) == std::array<uint32_t, 2>{3, 512}, "HiDPI picking missed terrain surface");
    Check(PickGridCell(view, {15.99, 31.99}, {800, 600}, {800, 600}) == std::array<uint32_t, 2>{2, 511}, "picking rounded up at cell boundary");
    view.CellSizePixels = {8, 8};
    view.OriginPixels = {-1, 0};
    Check(!PickGridCell(view, {0, 0}, {800, 600}, {1600, 1200}), "negative world coordinate wrapped to cell zero");
    Check(PickGridCell(view, {0.5, 4}, {800, 600}, {1600, 1200}) == std::array<uint32_t, 2>{0, 1}, "zoomed picking failed");
    Check(!PickGridCell(view, {-1, 0}, {800, 600}, {1600, 1200}) && !PickGridCell(view, {800, 0}, {800, 600}, {1600, 1200}), "outside-window click accepted");
    Check(!PickGridCell(view, {0, 0}, {800, 600}, {0, 0}), "minimized framebuffer accepted");
    view.OriginPixels = {0, 1024 * 8};
    Check(!PickGridCell(view, {0, 0}, {800, 600}, {1600, 1200}), "click below world accepted");
    view.OriginPixels = {1024 * 8 - 1, 1024 * 8 - 1};
    Check(PickGridCell(view, {0, 0}, {800, 600}, {1600, 1200}) == std::array<uint32_t, 2>{1023, 1023}, "last cell cannot be selected");
}
} // namespace
void RunPixelWorldTests() {
    WorldOperations();
    WorldPublication();
    PresentationMapping();
    TerrainAndGravity();
    GravityTiming();
    MousePicking();
}
