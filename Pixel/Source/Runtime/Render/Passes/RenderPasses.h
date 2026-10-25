#pragma once

#include "RenderResources.h"
#include <functional>
#include <string>

namespace pixel::render {
class UploadPass {
  public:
    void Prepare(DynamicRHI &rhi, const RenderResources &resources, FrameResources &frame, const WorldGridFrameView &input);
    void Record(RHICommandList &commands, const RenderPassContext &context) const;
    // Update upload caches only after the complete frame records successfully.
    uint64_t Commit(FrameResources &frame, const WorldGridFrameView &input);
    void Reset() { AssetsUploaded = false; }

  private:
    bool AssetsUploaded = false;
};

class WorldPass {
  public:
    std::unique_ptr<RHIPipeline> CreatePipeline(DynamicRHI &rhi, RHITexture &target) const;
    void Record(RHICommandList &commands, const RenderPassContext &context) const;
};

class WindPass {
  public:
    bool Enabled(const WorldGridFrameView &input) const;
    std::unique_ptr<RHIPipeline> CreatePipeline(DynamicRHI &rhi, RHITexture &target) const;
    void Record(RHICommandList &commands, const RenderPassContext &context) const;
};

class SceneObjectsPass {
  public:
    bool Enabled(const WorldGridFrameView &input) const;
    std::unique_ptr<RHIPipeline> CreatePipeline(DynamicRHI &rhi, RHITexture &target) const;
    void Record(RHICommandList &commands, const RenderPassContext &context) const;
};

class CharacterPass {
  public:
    bool Enabled(const WorldGridFrameView &input) const;
    std::unique_ptr<RHIPipeline> CreatePipeline(DynamicRHI &rhi, RHITexture &target) const;
    void Record(RHICommandList &commands, const RenderPassContext &context) const;
};

class PostProcessPass {
  public:
    std::unique_ptr<RHIPipeline> CreatePipeline(DynamicRHI &rhi, const std::string &shader) const;
    void Record(RHICommandList &commands, const RenderPassContext &context,
                const std::function<void(RHICommandList &)> &overlay) const;
};
} // namespace pixel::render
