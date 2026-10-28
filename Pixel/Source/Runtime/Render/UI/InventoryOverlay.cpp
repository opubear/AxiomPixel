#include "InventoryOverlay.h"
#include "HAL/Shader/ShaderLibrary.h"

namespace pixel::render {
void InventoryOverlay::Initialize(DynamicRHI &rhi, Publisher publisher) {
    Publish = std::move(publisher);
    if (!Publish) return;
    const auto slots = rhi.GetFrameSlotCount();
    Pool = rhi.CreateImageDescriptorPool(slots, slots, 0);
    Frames.resize(slots);
    for (auto &frame : Frames) {
        frame.Texture = rhi.CreateTexture({ui::InventoryLayout::Width, ui::InventoryLayout::Height, 1, RHITextureFormat::RGBA8SRGB});
        frame.Upload = rhi.CreateUploadBuffer(size_t(ui::InventoryLayout::Width) * ui::InventoryLayout::Height * 4);
    }
    RecreatePipeline(rhi);
}

void InventoryOverlay::RecreatePipeline(DynamicRHI &rhi) {
    if (!Publish) return;
    for (auto &frame : Frames) frame.Set.reset();
    Pipeline.reset();
    constexpr std::array bindings{DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1}};
    Pipeline = rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"), GetShaderInfo("PixelInventoryUI.fs"), bindings, nullptr, 16});
    for (auto &frame : Frames) {
        const std::array images{RHIImageBinding{0, frame.Texture.get()}};
        frame.Set = rhi.CreateImageDescriptorSet(*Pool, *Pipeline, images);
    }
}

void InventoryOverlay::Prepare(uint32_t slot) {
    if (!Publish) return;
    const auto &canvas = Publish();
    auto &frame = Frames.at(slot);
    if (!canvas.Revision || canvas.Pixels.size() != size_t(ui::InventoryLayout::Width) * ui::InventoryLayout::Height * 4)
        throw std::invalid_argument("invalid inventory UI canvas");
    PendingRevision = canvas.Revision;
    if (frame.Revision != PendingRevision) frame.Upload->Write(canvas.Pixels);
}

void InventoryOverlay::Upload(RHICommandList &commands, uint32_t slot) {
    if (!Publish) return;
    auto &frame = Frames.at(slot);
    if (frame.Revision == PendingRevision) return;
    commands.TransitionTexture(*frame.Texture, RHITextureState::TransferDestination);
    commands.CopyBufferToTexture(*frame.Upload, *frame.Texture);
    commands.TransitionTexture(*frame.Texture, RHITextureState::FragmentSampledRead);
}

void InventoryOverlay::Draw(RHICommandList &commands, uint32_t slot, RHIPresentationExtent extent) {
    if (!Publish) return;
    const auto placement = ui::InventoryLayout::Placement({int(extent.Width), int(extent.Height)});
    const std::array<float, 4> bounds{placement.X, placement.Y, placement.Width, placement.Height};
    commands.SetViewport({placement.X, placement.Y, placement.Width, placement.Height, 0, 1});
    commands.SetScissor({0, 0, extent.Width, extent.Height});
    commands.BindPipeline(*Pipeline);
    commands.BindDescriptorSet(*Pipeline, *Frames.at(slot).Set, 0);
    commands.PushConstants(*Pipeline, RHIShaderStage::Fragment, 0, sizeof(bounds), bounds.data());
    commands.DrawIndexed(6);
    commands.SetViewport({0, 0, float(extent.Width), float(extent.Height), 0, 1});
}

void InventoryOverlay::Commit(uint32_t slot) {
    if (Publish) Frames.at(slot).Revision = PendingRevision;
}

void InventoryOverlay::Clear() {
    Publish = {};
    Frames.clear();
    Pool.reset();
    Pipeline.reset();
    PendingRevision = 0;
}
}
