#pragma once

#include "HAL/RenderHardwareInterface/RenderHardwareInterface.h"
#include "Assets/ArtAssets.h"
#include "PixelRender/WorldGridFrame.h"
#include "Render/RenderPolicy/RenderPolicyInterface.h"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The publisher fills owned CPU data once at the start of a drawable frame.
class PixelRender final : public RenderPolicyInterface {
  public:
    PixelRender(DynamicRHI &rhi, GridTextureCatalog catalog,
                std::function<void(WorldGridFrame &)> input,
                std::string postShader = "Pixel2DPostProcess.fs",
                std::shared_ptr<const pixel::BackgroundAssets> background = {});
    ~PixelRender() override;
    void Initialize() override;
    void FrameBegin() override;
    void Draw() override;
    void OnPresentationChanged(const RHIPresentationChanged &) override;
    void Clear() override;
    uint64_t SubmittedFrames() const { return FrameCount; }
    uint64_t UploadedWorldBytes() const { return WorldUploadBytes; }
    pixel::performance::MemoryUsage FormulaPresentationMemory() const {
        return Published ? Published->GetFormulaMemoryUsage() : pixel::performance::MemoryUsage{};
    }
    pixel::performance::MemoryUsage DevicePresentationMemory() const {
        return Published ? Published->GetDeviceMemoryUsage() : pixel::performance::MemoryUsage{};
    }
    uint64_t FormulaTargetBytes() const { return uint64_t(Extent.Width) * Extent.Height * 4 * Frames.size(); }
    const RHITexture &SceneColor(uint32_t slot) const;
    const RHITexture &WorldColor(uint32_t slot) const;
    // Borrowed until the next FrameBegin or Clear, including after Draw.
    WorldGridFrameView PublishedFrame() const;

  private:
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
    DynamicRHI &RHI;
    GridTextureCatalog Catalog;
    std::function<void(WorldGridFrame &)> Input;
    std::unique_ptr<WorldGridFrame> Published;
    bool FrameReady = false;
    std::string PostShader;
    std::unique_ptr<RHIBuffer> Vertices, Indices;
    std::unique_ptr<RHISampler> Sampler;
    std::unique_ptr<RHITexture> Materials;
    std::unique_ptr<RHIUploadBuffer> MaterialUpload;
    std::shared_ptr<const pixel::BackgroundAssets> Background;
    std::unique_ptr<RHITexture> BackgroundTexture;
    std::unique_ptr<RHIUploadBuffer> BackgroundUpload;
    std::unique_ptr<RHIPipeline> WorldPipeline, WindPipeline, FormulaPipeline, CharacterPipeline, PostPipeline;
    std::unique_ptr<RHIDescriptorPool> Pool;
    std::vector<FrameResources> Frames;
    RHIPresentationExtent Extent{};
    bool MaterialsUploaded = false;
    uint64_t FrameCount = 0;
    uint64_t WorldUploadBytes = 0;
    void CreateColorTargets();
    void CreateGridResources(FrameResources &frame, uint32_t width, uint32_t height);
    void PrepareGridUpload(FrameResources &frame, const WorldGridFrameView &input);
    void RecordWorld(RHICommandList &commands, FrameResources &frame, const WorldGridFrameView &input);
    void RecordWind(RHICommandList &commands, FrameResources &frame, const WorldGridFrameView &input);
    void RecordFormula(RHICommandList &commands, FrameResources &frame, const WorldGridFrameView &input);
    void RecordCharacter(RHICommandList &commands, FrameResources &frame, const CharacterDraw &character);
    void RecordPost(RHICommandList &commands, FrameResources &frame, const PostProcessParameters &parameters);
    void SetFullViewport(RHICommandList &commands);
};
