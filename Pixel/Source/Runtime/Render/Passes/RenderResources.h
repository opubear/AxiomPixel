#pragma once

#include "HAL/RenderHardwareInterface/RenderHardwareInterface.h"
#include "Runtime/Render/Assets/ArtAssets.h"
#include "Runtime/Render/WorldGridFrame.h"
#include <memory>
#include <vector>

namespace pixel::render {
// Each in-flight slot owns its writable images, descriptors and upload versions.
struct FrameResources {
    std::unique_ptr<RHITexture> Data;
    std::unique_ptr<RHIUploadBuffer> Upload;
    std::unique_ptr<RHITexture> Color, WindColor, FormulaColor, CharacterColor, WindData;
    std::unique_ptr<RHIUploadBuffer> WindUpload;
    bool HasWind = false;
    bool HasCharacter = false;
    bool HasFormula = false;
    std::unique_ptr<RHIDescriptorSet> WorldSet;
    std::unique_ptr<RHIDescriptorSet> PostSet, CharacterSet, CharacterPostSet;
    std::unique_ptr<RHIDescriptorSet> WindSet, WindPostSet, WindCharacterSet;
    std::unique_ptr<RHIDescriptorSet> FormulaSet, WindFormulaSet, FormulaPostSet, FormulaCharacterSet;
    std::array<int64_t, 2> CellOrigin{};
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;
    std::vector<uint64_t> RegionVersions, WindRegionVersions;
    std::vector<RHITextureCopyRegion> DirtyRegions, WindDirtyRegions;
};

// PixelRender owns resource lifetime; concrete passes borrow resources while recording.
struct RenderResources {
    std::unique_ptr<RHIBuffer> Vertices, Indices;
    std::unique_ptr<RHISampler> Sampler;
    std::unique_ptr<RHITexture> Materials;
    std::unique_ptr<RHIUploadBuffer> MaterialUpload;
    std::unique_ptr<RHITexture> BackgroundTexture;
    std::unique_ptr<RHIUploadBuffer> BackgroundUpload;
    std::unique_ptr<RHITexture> CharacterTexture;
    std::unique_ptr<RHIUploadBuffer> CharacterUpload;
    std::unique_ptr<RHIPipeline> WorldPipeline, WindPipeline, FormulaPipeline, CharacterPipeline, PostPipeline;
    std::unique_ptr<RHIDescriptorPool> Pool;
    std::vector<FrameResources> Frames;
    RHIPresentationExtent Extent{};
};

struct RenderPassContext {
    const RenderResources &Resources;
    const FrameResources &Frame;
    const WorldGridFrameView &Input;
    const GridTextureCatalog &Catalog;
    const BackgroundAssets *Background;

    void BindFullscreen(RHICommandList &commands) const;
};
} // namespace pixel::render
