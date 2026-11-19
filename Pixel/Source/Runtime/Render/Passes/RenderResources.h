#pragma once

#include "HAL/RenderHardwareInterface/RenderHardwareInterface.h"
#include "Runtime/Render/Assets/ArtAssets.h"
#include "Runtime/Render/WorldGridFrame.h"
#include <memory>
#include <vector>

namespace pixel::render {
struct LightmapSlot {
    std::unique_ptr<RHIDescriptorPool> Pool;
    std::unique_ptr<RHITexture> Texture;
    std::unique_ptr<RHIUploadBuffer> Upload;
    std::array<std::unique_ptr<RHIDescriptorSet>, 4> Sets;
    std::array<int64_t, 2> CellOrigin{};
    std::vector<uint64_t> Versions;
    std::vector<RHITextureCopyRegion> Dirty;
};

// Each in-flight slot owns its writable images, descriptors and upload versions.
struct FrameResources {
    std::unique_ptr<RHITexture> Data;
    std::unique_ptr<RHIUploadBuffer> Upload;
    std::unique_ptr<RHITexture> Color, WindColor, FormulaColor, CharacterColor, MonsterColor, WindData;
    std::unique_ptr<RHIUploadBuffer> WindUpload;
    bool HasWind = false;
    bool HasCharacter = false;
    bool HasFormula = false;
    std::unique_ptr<RHIDescriptorSet> WorldSet;
    std::unique_ptr<RHIDescriptorSet> PostSet, CharacterSet, CharacterPostSet;
    std::unique_ptr<RHIDescriptorSet> WindSet, WindPostSet, WindCharacterSet;
    std::unique_ptr<RHIDescriptorSet> FormulaSet, WindFormulaSet, FormulaPostSet, FormulaCharacterSet;
    std::unique_ptr<RHIDescriptorSet> FormulaToMonsterSet, MonsterToFormulaSet;
    std::array<int64_t, 2> CellOrigin{};
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;
    std::vector<uint64_t> RegionVersions, WindRegionVersions;
    std::vector<RHITextureCopyRegion> DirtyRegions, WindDirtyRegions;
    std::unique_ptr<LightmapSlot> Lights;

    size_t SceneIndex() const {
        if (HasCharacter) return 3;
        if (HasFormula) return 2;
        return HasWind ? 1 : 0;
    }
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
    std::unique_ptr<RHITexture> MonsterTexture;
    std::unique_ptr<RHIUploadBuffer> MonsterUpload;
    std::unique_ptr<RHIPipeline> WorldPipeline, WindPipeline, FormulaPipeline, CharacterPipeline, PostPipeline;
    std::unique_ptr<RHIPipeline> LightPostPipeline;
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

void StageGridUpload(RHIUploadBuffer &upload, const RHITextureDesc &texture, std::span<const std::byte> texels,
                     uint32_t tileEdge, std::span<const uint64_t> versions, std::span<const uint64_t> uploaded,
                     std::vector<RHITextureCopyRegion> &dirty);
} // namespace pixel::render
