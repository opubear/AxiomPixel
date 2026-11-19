#pragma once

#include "Runtime/Simulation/World/WorldCoordinates.h"
#include <array>
#include <optional>
#include <stop_token>
#include <vector>

namespace pixel::render {
// Lighting geometry is independent of collision and texture alpha.
enum class OpticalCell : uint8_t { Missing, Wall, Clear, Water, Ice, Leaves, Torch, Fire, Lava };
using LightTexel = std::array<uint8_t, 4>;
inline constexpr uint32_t LightRadius = 16;
inline constexpr uint32_t LightUnit = 64; // Texel RGB is linear light in units of 1/64.
inline constexpr uint32_t LightTileEdge = storage::RegionEdge;
inline constexpr uint32_t LightPatchEdge = LightTileEdge + 2 * LightRadius;
static_assert(LightRadius <= LightTileEdge);

// A tile and its full dependency border. Missing cells close the boundary.
struct LightPatch {
    std::array<OpticalCell, LightPatchEdge * LightPatchEdge> Cells{};
    std::optional<size_t> HeldTorch;
};

enum class LightSources { Placed, Dynamic };
std::vector<LightTexel> FloodLightTile(const LightPatch &patch, LightSources sources);
std::vector<LightTexel> BakeLightTile(const LightPatch &patch, uint32_t samplesPerCell,
                                     std::stop_token stop = {});
} // namespace pixel::render
