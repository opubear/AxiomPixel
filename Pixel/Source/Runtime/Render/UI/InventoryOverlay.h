#pragma once
#include "Runtime/Gameplay/UI/InventoryUI.h"
#include "HAL/RenderHardwareInterface/RenderHardwareInterface.h"
#include <functional>

namespace pixel::render {
// Transport only: native UI state, layout, hit testing and painting live in Gameplay/UI.
class InventoryOverlay {
  public:
    using Publisher = std::function<const ui::Canvas &()>;
    void Initialize(DynamicRHI &rhi, Publisher publisher);
    void RecreatePipeline(DynamicRHI &rhi);
    void Prepare(uint32_t slot);
    void Upload(RHICommandList &commands, uint32_t slot);
    void Draw(RHICommandList &commands, uint32_t slot, RHIPresentationExtent extent);
    void Commit(uint32_t slot);
    void Clear();

  private:
    struct Frame {
        std::unique_ptr<RHITexture> Texture;
        std::unique_ptr<RHIUploadBuffer> Upload;
        std::unique_ptr<RHIDescriptorSet> Set;
        uint64_t Revision = 0;
    };
    Publisher Publish;
    std::unique_ptr<RHIPipeline> Pipeline;
    std::unique_ptr<RHIDescriptorPool> Pool;
    std::vector<Frame> Frames;
    uint64_t PendingRevision = 0;
};
}
