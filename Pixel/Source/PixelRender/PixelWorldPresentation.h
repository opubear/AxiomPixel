#pragma once

#include "PixelRender/WorldGridFrame.h"
#include "World/WindField.h"
#include "World/WorldState.h"
#include <unordered_map>

// This sample-specific presentation boundary is the only place where gameplay
// type identities are interpreted as visual assets. Empty space always maps to 0.
using CellAppearanceTable = std::unordered_map<uint32_t, uint32_t>;
void BuildWorldGridFrame(pixel::WorldStateView world, const CellAppearanceTable &appearances, WorldGridFrame &output);

namespace pixel {
class World;
}
// Unit direction [-1,1] -> linear RGB [0,1]; zero is neutral grey.
std::array<float, 3> FieldDirectionColor(pixel::WindVelocity velocity);
// Called at publication only. Copies colours into render-owned storage; no
// World/Wind references survive into Draw, and empty cells also display the field.
void BuildForceFieldFrame(const pixel::World &world, WorldGridFrame &output);
