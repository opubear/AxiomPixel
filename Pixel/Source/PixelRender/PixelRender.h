#pragma once

#include "HAL/RenderHardwareInterface/RenderHardwareInterface.h"
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
                std::string postShader = "Pixel2DPostProcess.fs");
    ~PixelRender() override;
    void Initialize() override;
    void FrameBegin() override;
    void Draw() override;
    void OnPresentationChanged() override;
    void Clear() override;
    // Optional UI recording inside the present pass, after world post-processing.
    void SetOverlay(std::function<void(RHICommandList &)> overlay) { Overlay = std::move(overlay); }
    uint64_t SubmittedFrames() const { return FrameCount; }
    uint64_t UploadedWorldBytes() const { return FrameCount * Grid2D::UploadBytes; }
    const RHITexture &SceneColor(uint32_t slot) const;
    // Borrowed until the next FrameBegin or Clear, including after Draw.
    WorldGridFrameView PublishedFrame() const;

  private:
    struct FrameResources {
        std::array<std::unique_ptr<RHITexture>, Grid2D::PageCount> Data;
        std::unique_ptr<RHIUploadBuffer> Upload;
        std::unique_ptr<RHITexture> Color;
        std::unique_ptr<RHIDescriptorSet> WorldSet;
        std::unique_ptr<RHIDescriptorSet> PostSet;
    };
    DynamicRHI &RHI;
    GridTextureCatalog Catalog;
    std::function<void(WorldGridFrame &)> Input;
    std::function<void(RHICommandList &)> Overlay;
    std::unique_ptr<WorldGridFrame> Published;
    bool FrameReady = false;
    std::string PostShader;
    std::unique_ptr<RHIBuffer> Vertices, Indices;
    std::unique_ptr<RHISampler> Sampler;
    std::unique_ptr<RHITexture> Materials;
    std::unique_ptr<RHIUploadBuffer> MaterialUpload;
    std::unique_ptr<RHIPipeline> WorldPipeline, PostPipeline;
    std::unique_ptr<RHIDescriptorPool> Pool;
    std::vector<FrameResources> Frames;
    RHIPresentationExtent Extent{};
    bool MaterialsUploaded = false;
    uint64_t FrameCount = 0;
    void CreateColorTargets();
    void RecordWorld(RHICommandList &commands, FrameResources &frame, const GridView2D &view, GridDisplayMode mode);
    void RecordPost(RHICommandList &commands, FrameResources &frame, const PostProcessParameters &parameters);
    void SetFullViewport(RHICommandList &commands);
};
