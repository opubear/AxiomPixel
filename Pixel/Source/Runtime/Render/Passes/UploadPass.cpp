#include "Dev/Debug/Performance/FrameProfiler.h"
#include "RenderPasses.h"
#include <algorithm>

namespace pixel::render {
namespace {
void CreateGridResources(DynamicRHI &rhi, const RenderResources &resources, FrameResources &frame, uint32_t width, uint32_t height) {
    // The active frame slot has completed on the GPU. Other slots keep their
    // descriptors and images until their own next use.
    frame.WorldSet.reset();
    frame.WindSet.reset();
    frame.Data = rhi.CreateTexture({width, height, 1, RHITextureFormat::R32UInt});
    frame.WindData = rhi.CreateTexture({width, height, 1, RHITextureFormat::R32UInt});
    const size_t bytes = size_t(width) * height * sizeof(uint32_t);
    frame.Upload = rhi.CreateUploadBuffer(bytes);
    frame.WindUpload = rhi.CreateUploadBuffer(bytes);
    const std::array bindings{RHIImageBinding{0, frame.Data.get()}, RHIImageBinding{1, resources.Materials.get()},
                              RHIImageBinding{2, nullptr, resources.Sampler.get()},
                              RHIImageBinding{3, resources.BackgroundTexture ? resources.BackgroundTexture.get() : resources.Materials.get()}};
    frame.WorldSet = rhi.CreateImageDescriptorSet(*resources.Pool, *resources.WorldPipeline, bindings);
    const std::array windBindings{RHIImageBinding{0, frame.Color.get()}, RHIImageBinding{1, frame.WindData.get()}};
    frame.WindSet = rhi.CreateImageDescriptorSet(*resources.Pool, *resources.WindPipeline, windBindings);
    frame.RegionVersions.clear();
    frame.WindRegionVersions.clear();
}
} // namespace
void UploadPass::Prepare(DynamicRHI &rhi, const RenderResources &resources, FrameResources &frame, const WorldGridFrameView &input) {
    PIXEL_PROFILE_SCOPE(Upload);
    if (frame.Data->Desc.Width != input.Width || frame.Data->Desc.Height != input.Height)
        CreateGridResources(rhi, resources, frame, input.Width, input.Height);
    const bool relocated = frame.CellOrigin != input.CellOrigin;
    if (relocated)
        frame.WindRegionVersions.clear();
    const auto prepare = [&](RHIUploadBuffer &upload, std::span<const uint32_t> cells,
                             std::span<const uint64_t> versions, const std::vector<uint64_t> &cached,
                             std::vector<RHITextureCopyRegion> &dirty) {
        dirty.clear();
        const bool full = versions.empty() || relocated || cached.size() != versions.size();
        if (full) {
            upload.Write(std::as_bytes(cells));
            dirty.push_back({0, 0, 0, 0, 0, input.Width, input.Height});
            return;
        }
        const uint32_t columns = (input.Width + Grid2D::RegionSize - 1) / Grid2D::RegionSize;
        for (size_t index = 0; index < versions.size(); ++index) {
            if (cached[index] == versions[index])
                continue;
            const uint32_t x = uint32_t(index % columns) * Grid2D::RegionSize;
            const uint32_t y = uint32_t(index / columns) * Grid2D::RegionSize;
            const uint32_t width = std::min(Grid2D::RegionSize, input.Width - x);
            const uint32_t height = std::min(Grid2D::RegionSize, input.Height - y);
            for (uint32_t row = 0; row < height; ++row) {
                const size_t first = size_t(y + row) * input.Width + x;
                upload.WriteRegion(std::as_bytes(cells.subspan(first, width)), first * sizeof(uint32_t));
            }
            dirty.push_back({(size_t(y) * input.Width + x) * sizeof(uint32_t), input.Width, input.Height,
                             x, y, width, height});
        }
    };
    if (frame.DisplayMode != input.DisplayMode)
        frame.RegionVersions.clear();
    prepare(*frame.Upload, input.Cells, input.RegionVersions, frame.RegionVersions, frame.DirtyRegions);
    frame.WindDirtyRegions.clear();
    if (frame.HasWind)
        prepare(*frame.WindUpload, input.WindCells, input.WindRegionVersions, frame.WindRegionVersions, frame.WindDirtyRegions);
    const auto bytes = [](const auto &regions) {
        uint64_t total = 0;
        for (const auto &region : regions)
            total += uint64_t(region.Width) * region.Height * sizeof(uint32_t);
        return total;
    };
    pixel::performance::FrameProfiler::Count(pixel::performance::Counter::WorldUploadBytes, bytes(frame.DirtyRegions));
    pixel::performance::FrameProfiler::Count(pixel::performance::Counter::WindUploadBytes, bytes(frame.WindDirtyRegions));
}
void UploadPass::Record(RHICommandList &commands, const RenderPassContext &context) const {
    if (AssetsUploaded)
        return;
    const auto &resources = context.Resources;
    commands.TransitionTexture(*resources.Materials, RHITextureState::TransferDestination);
    commands.CopyBufferToTexture(*resources.MaterialUpload, *resources.Materials);
    commands.TransitionTexture(*resources.Materials, RHITextureState::FragmentSampledRead);
    if (resources.BackgroundTexture) {
        commands.TransitionTexture(*resources.BackgroundTexture, RHITextureState::TransferDestination);
        commands.CopyBufferToTexture(*resources.BackgroundUpload, *resources.BackgroundTexture);
        commands.TransitionTexture(*resources.BackgroundTexture, RHITextureState::FragmentSampledRead);
    }
    if (resources.CharacterTexture) {
        commands.TransitionTexture(*resources.CharacterTexture, RHITextureState::TransferDestination);
        commands.CopyBufferToTexture(*resources.CharacterUpload, *resources.CharacterTexture);
        commands.TransitionTexture(*resources.CharacterTexture, RHITextureState::FragmentSampledRead);
    }
}
uint64_t UploadPass::Commit(FrameResources &frame, const WorldGridFrameView &input) {
    AssetsUploaded = true;
    frame.RegionVersions.assign(input.RegionVersions.begin(), input.RegionVersions.end());
    if (frame.HasWind)
        frame.WindRegionVersions.assign(input.WindRegionVersions.begin(), input.WindRegionVersions.end());
    frame.CellOrigin = input.CellOrigin;
    frame.DisplayMode = input.DisplayMode;
    uint64_t bytes = 0;
    for (const auto &region : frame.DirtyRegions)
        bytes += uint64_t(region.Width) * region.Height * sizeof(uint32_t);
    return bytes;
}
} // namespace pixel::render
