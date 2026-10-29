#include "Runtime/Render/PixelWorldPresentation.h"
#include "../Support/SimulatedWorld.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {
using World = pixel::testing::SimulatedWorld;
std::vector<pixel::CellState> CopyCells(pixel::WorldMapView view) {
    std::vector<pixel::CellState> cells;
    cells.reserve(view.CellCount());
    for (size_t i = 0; i < view.CellCount(); ++i)
        cells.push_back(view.AtIndex(i));
    return cells;
}
bool SameCells(pixel::WorldMapView left, pixel::WorldMapView right) {
    return CopyCells(left) == CopyCells(right);
}
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
void CellInformation() {
    using namespace pixel;
    static_assert(std::is_enum_v<CellTypeId>);
    static_assert(std::is_same_v<decltype(CellState::Type), CellTypeId>);
    static_assert(sizeof(CellTypeId) == sizeof(uint32_t));
    static_assert(GetCellInfo(CellTypeId::Air).Mass == 0.f);
    static_assert(GetCellInfo(CellTypeId::Dirt).Mass == 1.f);
    World world(2, 2);
    const auto &air = GetCellInfo(world.GetCell(0, 0).Type);
    Check(&air == &CellDefinition<CellTypeId::Air>::Info, "air lookup copied static information");
    world.PlaceDirt(0, 0);
    WorldSnapshot snapshot(2, 2);
    world.PublishState(snapshot);
    const auto &dirt = GetCellInfo(snapshot.Read().AtIndex(0).Type);
    Check(&dirt == &CellDefinition<CellTypeId::Dirt>::Info, "dirt lookup copied static information");
    Check(&dirt == &GetCellInfo(world.GetCell(0, 0).Type), "world and snapshot do not share information");
    world.RemoveDirt(0, 0);
    Check(&GetCellInfo(world.GetCell(0, 0).Type) == &air, "removed dirt did not resolve to air");
    Check(dirt.Mass == 1.f, "world edit mutated shared information");
    Reject<std::invalid_argument>([] { GetCellInfo(static_cast<CellTypeId>(99)); });
}
void WorldOperations() {
    Reject<std::invalid_argument>([] { pixel::testing::SimulatedWorld world(0, 4); });
    Reject<std::invalid_argument>([] { pixel::testing::SimulatedWorld world(4, 0); });
    Reject<std::invalid_argument>([] { pixel::testing::SimulatedWorld world(UINT32_MAX, UINT32_MAX); });
    pixel::testing::SimulatedWorld world(3, 2);
    Check(world.ReadState().CellCount() == 6, "incorrect world extent");
    for (auto cell : CopyCells(world.ReadState()))
        Check(cell.Type == pixel::CellTypeId::Air, "world not initially empty");
    world.SetCell(2, 1, {pixel::CellTypeId::Dirt});
    Check(world.GetCell(2, 1).Type == pixel::CellTypeId::Dirt && world.ReadState().AtIndex(5).Type == pixel::CellTypeId::Dirt, "world indexing incorrect");
    Reject<std::out_of_range>([&] { world.SetCell(3, 0, {static_cast<pixel::CellTypeId>(20)}); });
    Reject<std::out_of_range>([&] { world.GetCell(0, 2); });
    Reject<std::out_of_range>([&] { world.GetCell(UINT32_MAX, 0); });
    world.Fill({static_cast<pixel::CellTypeId>(20)});
    world.Tick(0.016f);
    for (auto cell : CopyCells(world.ReadState()))
        Check(cell.Type == static_cast<pixel::CellTypeId>(20), "fill or empty character/environment tick changed world");
    world.Fill({});
    Check(world.GetCell(2, 1).Type == pixel::CellTypeId::Air, "world clearing failed");
}
void WorldPublication() {
    pixel::testing::SimulatedWorld world(3, 2);
    pixel::WorldSnapshot snapshot(3, 2);
    world.SetCell(0, 0, {pixel::CellTypeId::Dirt});
    world.PublishState(snapshot);
    Check(snapshot.Read().AtIndex(0).Type == pixel::CellTypeId::Dirt, "publication lost an existing edit");

    world.SetCell(2, 1, {static_cast<pixel::CellTypeId>(27)});
    Check(snapshot.Read().AtIndex(5).Type == pixel::CellTypeId::Air, "working edit changed the published snapshot");
    Check(world.ReadState().AtIndex(5).Type == static_cast<pixel::CellTypeId>(27), "working edit did not persist until publication");
    world.PublishState(snapshot);
    Check(snapshot.Read().AtIndex(5).Type == static_cast<pixel::CellTypeId>(27), "publication did not expose the persistent edit");

    world.SetCell(0, 0, {});
    world.PublishState(snapshot);
    world.PublishState(snapshot); // An unchanged publication must preserve the latest contents.
    Check(snapshot.Read().AtIndex(0).Type == pixel::CellTypeId::Air && snapshot.Read().AtIndex(5).Type == static_cast<pixel::CellTypeId>(27),
          "clear or unchanged publication reverted data");

    pixel::WorldSnapshot wrongShape(2, 3);
    const auto before = CopyCells(world.ReadState());
    Reject<std::invalid_argument>([&] { world.PublishState(wrongShape); });
    Check(CopyCells(world.ReadState()) == before, "rejected publication modified world");
}
void PresentationMapping() {
    pixel::testing::SimulatedWorld world(3, 2);
    pixel::WorldSnapshot snapshot(3, 2);
    WorldGridFrame output;
    CellAppearanceTable appearances{{10, 2}, {20, 2}};
    world.SetCell(0, 0, {pixel::CellTypeId::Dirt});
    world.SetCell(2, 1, {static_cast<pixel::CellTypeId>(20)});
    world.PublishState(snapshot);
    BuildWorldGridFrame(snapshot.Read(), appearances, output);
    Check(output.Read().Cells.front() == 2 && output.Read().Cells.back() == 2 && output.Read().Cells[1] == 0, "nonidentity or shared appearance mapping failed");
    const auto *renderStorage = output.Cells().data();
    appearances[10] = 3;
    BuildWorldGridFrame(snapshot.Read(), appearances, output);
    Check(output.Read().Cells.front() == 3 && output.Cells().data() == renderStorage, "mapping change did not reuse render storage");
    Check(world.GetCell(0, 0).Type == pixel::CellTypeId::Dirt && snapshot.Read().AtIndex(0).Type == pixel::CellTypeId::Dirt, "presentation changed gameplay identity");
    world.SetCell(0, 0, {static_cast<pixel::CellTypeId>(99)});
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
    malformed.Map = nullptr;
    malformed.Snapshot = nullptr;
    Reject<std::invalid_argument>([&] { BuildWorldGridFrame(malformed, appearances, output); });
}
void TerrainAndGravity() {
    constexpr pixel::CellState dirt{pixel::CellTypes::Dirt};
    pixel::testing::SimulatedWorld terrain(4, 1024);
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
    Check(terrain.GetCell(1, 511) == dirt && terrain.GetCell(1, 512).Type == pixel::CellTypes::Empty && terrain.GetCell(1, 513) == dirt,
          "an excavated vacancy must advance one block per simulation step");
    Check(terrain.GetCell(0, 511).Type == pixel::CellTypes::Empty && terrain.GetCell(0, 512) == dirt, "gravity moved a neighbouring column");
    terrain.GenerateTerrain(512);
    Reject<std::out_of_range>([&] { terrain.GenerateTerrain(1025); });
    Check(terrain.GetCell(1, 511).Type == pixel::CellTypes::Empty && terrain.GetCell(1, 512) == dirt, "reset did not restore terrain");

    pixel::testing::SimulatedWorld world(3, 6);
    world.PlaceDirt(0, 0);
    world.PlaceDirt(0, 1);
    world.PlaceDirt(2, 5);
    world.SetCell(1, 3, {static_cast<pixel::CellTypeId>(20)});
    Check(!world.RemoveDirt(1, 3) && !world.PlaceDirt(1, 3), "dirt editing overwrote another material");
    pixel::WorldSnapshot snapshot(3, 6);
    world.PublishState(snapshot);
    for (int i = 0; i < 20; ++i)
        world.Tick(1.f / 60);
    Check(world.GetCell(0, 4) == dirt && world.GetCell(0, 5) == dirt && world.GetCell(2, 5) == dirt, "stack escaped the bottom or failed to settle");
    const auto cellsAfterGravity = CopyCells(world.ReadState());
    Check(std::count(cellsAfterGravity.begin(), cellsAfterGravity.end(), dirt) == 3, "gravity lost or duplicated dirt");
    Check(snapshot.Read().AtIndex(0) == dirt && snapshot.Read().AtIndex(3) == dirt, "simulation mutated a published snapshot");
    world.PublishState(snapshot);
    Check(snapshot.Read().AtIndex(12) == dirt && snapshot.Read().AtIndex(15) == dirt, "publication lost settled blocks");
    pixel::testing::SimulatedWorld singleRow(1, 1);
    singleRow.PlaceDirt(0, 0);
    singleRow.Tick(1);
    Check(singleRow.GetCell(0, 0) == dirt, "single-row world lost its bottom block");
}
void GravityTiming() {
    pixel::testing::SimulatedWorld fast(1, 100), slow(1, 100);
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
    Check(SameCells(fast.ReadState(), slow.ReadState()), "gravity depends on frame rate");
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
    view.CellSizePixels = {16, 16};
    view.OriginPixels = {32, 512 * 16 - 32};
    Check(PickGridCell(view, {8, 16}, {800, 600}, {1600, 1200}) == std::array<int64_t, 2>{3, 512}, "HiDPI picking missed terrain surface");
    Check(PickGridCell(view, {15.99, 31.99}, {800, 600}, {800, 600}) == std::array<int64_t, 2>{2, 511}, "picking rounded up at cell boundary");
    view.CellSizePixels = {8, 8};
    view.OriginPixels = {-1, 0};
    Check(PickGridCell(view, {0, 0}, {800, 600}, {1600, 1200}) == std::array<int64_t, 2>{-1, 0},
          "negative world coordinate was not preserved");
    Check(PickGridCell(view, {0.5, 4}, {800, 600}, {1600, 1200}) == std::array<int64_t, 2>{0, 1}, "zoomed picking failed");
    Check(!PickGridCell(view, {-1, 0}, {800, 600}, {1600, 1200}) && !PickGridCell(view, {800, 0}, {800, 600}, {1600, 1200}), "outside-window click accepted");
    Check(!PickGridCell(view, {0, 0}, {800, 600}, {0, 0}), "minimized framebuffer accepted");
    view.OriginPixels = {0, 1024 * 8};
    Check(PickGridCell(view, {0, 0}, {800, 600}, {1600, 1200}) == std::array<int64_t, 2>{0, 1024},
          "camera pick did not retain the world coordinate below the former finite extent");
    view.OriginPixels = {1024 * 8 - 1, 1024 * 8 - 1};
    Check(PickGridCell(view, {0, 0}, {800, 600}, {1600, 1200}) == std::array<int64_t, 2>{1023, 1023}, "camera offset selected the wrong world cell");
}
} // namespace
void RunPixelWorldTests() {
    CellInformation();
    WorldOperations();
    WorldPublication();
    PresentationMapping();
    TerrainAndGravity();
    GravityTiming();
    MousePicking();
}
