#pragma once

#include "Camera/Camera2D.h"
#include "PixelRender/WorldGridFrame.h"
#include "Actors/Actors.h"
#include "World/Environment/Wind/WindField.h"
#include "World/WorldMap.h"
#include <unordered_map>

// This sample-specific presentation boundary is the only place where gameplay
// type identities are interpreted as visual assets. Empty space always maps to 0.
struct CellAppearance {
    uint32_t Layer = 0;
    uint32_t SurfaceLayer = 0;
    uint32_t SurfaceStrip = 0;
    // Preserve the convenient {gameplayId, layer} form used by CPU fixtures.
    constexpr CellAppearance(uint32_t layer = 0, uint32_t surfaceLayer = 0, uint32_t surfaceStrip = 0)
        : Layer(layer), SurfaceLayer(surfaceLayer), SurfaceStrip(surfaceStrip) {}
};
using CellAppearanceTable = std::unordered_map<uint32_t, CellAppearance>;
void BuildWorldGridFrame(pixel::WorldMapView world, const CellAppearanceTable &appearances, WorldGridFrame &output);
uint64_t GridSourceRegionVersion(pixel::WorldMapView world, uint32_t x, uint32_t y, uint32_t width, uint32_t height);

namespace pixel {
class World;
class CommittedWorldView;
}
// Unit direction [-1,1] -> linear RGB [0,1]; zero is neutral grey.
std::array<float, 3> FieldDirectionColor(pixel::WindVelocity velocity);
// Called at publication only. Copies colours into render-owned storage; no
// World/Wind references survive into Draw, and empty cells also display the field.
void BuildForceFieldFrame(const pixel::World &world, WorldGridFrame &output);
void BuildForceFieldFrame(const pixel::CommittedWorldView &world, WorldGridFrame &output);

// Publish continuous geometry independently from material cells, using the exact camera centre.
void BuildCharacterFrame(const pixel::ActorSnapshot &character, const pixel::Camera2D &camera,
                         std::array<int, 2> framebufferSize, WorldGridFrame &output);

// Camera position is authoritative; pixel rounding happens only at presentation.
GridView2D MakeCameraView(const pixel::Camera2D &camera, std::array<int, 2> framebufferSize,
                          GridView2D settings);
pixel::WorldRect VisibleWorldRegion(const GridView2D &view, std::array<int, 2> framebufferSize);
