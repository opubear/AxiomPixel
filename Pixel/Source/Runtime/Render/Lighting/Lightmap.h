#pragma once

#include "LightSolver.h"
#include <span>

namespace pixel::render {
struct LightmapView {
    std::span<const LightTexel> Texels;
    std::span<const uint64_t> RegionVersions;
    uint32_t SamplesPerCell = 0;
    std::array<float, 3> Tint{1, 1, 1};
};

// Render-owned linear RGB on the same world rectangle as the material grid.
struct Lightmap {
    LightmapView Read() const {
        return Enabled ? LightmapView{Texels, RegionVersions, SamplesPerCell, Tint} : LightmapView{};
    }
    bool Enabled = false;
    uint32_t SamplesPerCell = 0;
    std::array<float, 3> Tint{1, 1, 1};
    std::vector<LightTexel> Texels;
    std::vector<uint64_t> RegionVersions;
};
} // namespace pixel::render
