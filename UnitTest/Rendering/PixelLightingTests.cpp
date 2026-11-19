#include "../Support/TestAccess.h"
#include "Runtime/Render/Lighting/LightingCache.h"
#include "Runtime/Render/GamePresentation.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace pixel;
using namespace pixel::render;

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

OpticalCell &Cell(LightPatch &patch, uint32_t x, uint32_t y) {
    return patch.Cells[size_t(y + LightRadius) * LightPatchEdge + x + LightRadius];
}

void ShadowAndPropagation() {
    LightPatch patch;
    patch.Cells.fill(OpticalCell::Clear);
    Cell(patch, 10, 10) = OpticalCell::Torch;
    for (uint32_t y = 5; y <= 15; ++y) Cell(patch, 13, y) = OpticalCell::Wall;
    const auto baked = BakeLightTile(patch, 2);
    const auto fast = FloodLightTile(patch, LightSources::Placed);
    const auto exact = [&](uint32_t x, uint32_t y) { return baked[y * 2 * LightTileEdge * 2 + x * 2][0]; };
    Require(exact(12, 10) > 0 && exact(13, 10) > 0 && exact(14, 10) == 0,
        "a one-cell wall did not stop direct light while receiving surface light");
    Require(fast[10 * LightTileEdge + 14][0] == 0 && fast[6 * LightTileEdge + 14][0] > 0,
        "flood light crossed the wall or failed to go around its open end");

    Cell(patch, 11, 10) = OpticalCell::Wall;
    Cell(patch, 10, 11) = OpticalCell::Wall;
    Require(BakeLightTile(patch, 1)[11 * LightTileEdge + 11][0] == 0,
        "direct light crossed a closed diagonal corner");
    Cell(patch, 11, 10) = OpticalCell::Clear;
    Cell(patch, 10, 11) = OpticalCell::Clear;
    const auto dry = FloodLightTile(patch, LightSources::Placed)[10 * LightTileEdge + 12];
    Cell(patch, 11, 10) = OpticalCell::Water;
    const auto wet = FloodLightTile(patch, LightSources::Placed)[10 * LightTileEdge + 12];
    Require(wet[0] < dry[0] && wet[0] > 0, "water did not absorb a portion of the flood light");

    Cell(patch, 9, 10) = OpticalCell::Torch;
    const auto both = BakeLightTile(patch, 1)[10 * LightTileEdge + 10];
    Require(both[0] > LightUnit && FloodLightTile(patch, LightSources::Placed)[10 * LightTileEdge + 10][0] == LightUnit,
        "static light should add while equal-colour flood light should share a maximum");
    // Dense sources share one propagation rather than one grid or queue each.
    for (uint32_t y = 0; y < LightTileEdge; y += 2)
        for (uint32_t x = 0; x < LightTileEdge; x += 2) Cell(patch, x, y) = OpticalCell::Lava;
    const auto dense = FloodLightTile(patch, LightSources::Dynamic);
    Require(dense[0][0] > dense[1][0] && dense.back()[0] > 0, "dense emitters did not propagate together");
}

void CrossTileInvalidation() {
    World world(128, 128);
    const WorldRect bounds{0, 0, 128, 128};
    const WorldRect visible{0, 0, 128, 64};
    LightingCache lights;
    Lightmap map;
    const auto publish = [&](std::optional<CellCoord> held = {}) { lights.Build(world.GetMap().View(bounds), visible, held, map); };
    const auto red = [&](size_t x, size_t y) {
        const auto scale = map.SamplesPerCell;
        return map.Texels.at(y * scale * bounds.Width * scale + x * scale)[0];
    };
    world.SetMaterial({60, 40}, CellTypeId::LitTorch);
    publish();
    Require(map.Enabled && red(66, 40) > 0, "placed light did not cross the 64-cell tile boundary");
    const auto preview = map.RegionVersions.at(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (map.RegionVersions[1] == preview && std::chrono::steady_clock::now() < deadline) {
        publish();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(map.RegionVersions[1] != preview, "background bake never replaced the preview");
    world.FillRegion({63, 24, 1, 33}, {CellTypeId::Stone});
    publish();
    Require(red(62, 40) > 0 && red(66, 40) == 0, "editing a neighbouring tile left stale light behind the wall");
    const auto edited = map.RegionVersions[1];
    world.SetMaterial({63, 40}, CellTypeId::Dirt);
    publish();
    Require(map.RegionVersions[1] == edited, "equally opaque materials invalidated the light bake");
    publish(); // Allow a closed-wall bake to start, then change its inputs.
    world.SetMaterial({63, 40}, CellTypeId::Air);
    publish();
    Require(red(66, 40) > 0, "opening a doorway did not restore light in the first preview");
    for (int i = 0; i < 12; ++i) {
        publish();
        Require(red(66, 40) > 0, "a stale closed-wall bake replaced the open doorway");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    world.SetMaterial({60, 40}, CellTypeId::UnlitTorch);
    publish(CellCoord{70, 40});
    Require(map.Enabled && red(70, 40) == LightUnit, "extinguishing did not remove the static contribution");
    publish(CellCoord{100, 40});
    Require(red(70, 40) == 0 && red(100, 40) == LightUnit, "moving a torch left a light trail");
    publish();
    Require(!map.Enabled, "removing the last dynamic light did not clear the lightmap");
}

void TorchActionsAndPublication() {
    GameSession session;
    const items::ItemRef torch{items::ItemId::Torch};
    auto &inventory = testing::SessionAccess::Inventory(session);
    const std::array added{items::ItemAmount{torch.Item, 1}};
    Require(inventory.Add(inventory.Reference(session.PlayerInventory()), added) == items::Result::Applied,
        "could not grant the test torch");
    const auto use = [&](api::Cell aim) { return session.Execute(NativeCaller::Input, api::UseHeldItem{torch, {aim, {0, 0}}}); };
    constexpr api::Cell placed{519, 511};
    Require(use({519, 509}).Rejection == api::Reason::Blocked && inventory.ContainsItem(session.PlayerInventory(), torch),
        "placement on unsupported ground spent inventory");
    Require(use({519, 512}).Applied() && session.GetWorld().GetCell(placed.X, placed.Y).Type == CellTypeId::LitTorch
        && !inventory.ContainsItem(session.PlayerInventory(), torch), "aiming at ground did not place and consume one torch");

    GameCamera camera;
    camera.SetFramebufferSize({800, 600});
    camera.Reset(session.GetWorld().GetSimulationBounds(), session.GetPlayer());
    GamePresentation presentation;
    auto publish = presentation.CreateFramePublisher(session, camera, {}, nullptr, {}, [] { return true; });
    WorldGridFrame frame;
    publish(frame);
    const auto at = size_t(placed.Y - frame.GetCellOrigin()[1]) * frame.GetWidth() + size_t(placed.X - frame.GetCellOrigin()[0]);
    Require(frame.Cells()[at] == Grid2D::LitTorchCell && frame.Read().Lights.SamplesPerCell > 0,
        "committed torch did not publish a sprite and a light");

    Require(session.Execute(NativeCaller::Input, api::ToggleTorch{placed}).Applied(), "extinguishing the torch failed");
    publish(frame);
    Require(frame.Cells()[at] == Grid2D::UnlitTorchCell && !frame.Read().Lights.SamplesPerCell,
        "extinguished torch remained lit or the stale hotbar selection emitted from an empty bag");
    Require(session.Execute(NativeCaller::Input, api::MineRegion{placed, {}}).Applied()
        && inventory.ContainsItem(session.PlayerInventory(), torch), "retrieving the unlit torch did not restore inventory");
    publish(frame);
    Require(frame.Read().Lights.SamplesPerCell > 0 && frame.Cells()[at] == 0,
        "retrieved torch did not move its light to the player");
    presentation.ToggleFieldView();
    publish(frame);
    Require(!frame.Read().Lights.SamplesPerCell, "F1 did not bypass lighting");
}
} // namespace

int main() {
    try {
        ShadowAndPropagation();
        CrossTileInvalidation();
        TorchActionsAndPublication();
        std::cout << "Pixel lighting tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Pixel lighting: " << error.what() << '\n';
        return 1;
    }
}
