#pragma once
#include "Runtime/Gameplay/UI/Canvas.h"
#include "HAL/RenderHardwareInterface/RenderHardwareInterface.h"
#include <functional>
#include <array>

namespace pixel::render {
// Transport only: native UI state, layout, hit testing and painting live in Gameplay/UI.
class CanvasOverlay {
  public:
    using Publisher = std::function<const ui::Canvas &()>;
    struct Layout {
        int Width, Height;
        ui::Rect (*Placement)(std::array<int, 2>, float);
    };
    void Initialize(DynamicRHI &rhi, Publisher publisher, Layout layout);
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
        float UIScale = 1;
        bool Visible = false;
    };
    Publisher Publish;
    Layout Geometry{};
    std::unique_ptr<RHIPipeline> Pipeline;
    std::unique_ptr<RHIDescriptorPool> Pool;
    std::vector<Frame> Frames;
    uint64_t PendingRevision = 0;
};
}
