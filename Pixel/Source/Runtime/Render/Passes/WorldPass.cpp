#include "Dev/Debug/Performance/FrameProfiler.h"
#include "HAL/Shader/ShaderLibrary.h"
#include "RenderPasses.h"
#include "Runtime/Render/BackgroundView.h"
#include <cstddef>

namespace pixel::render {
namespace {
constexpr std::array WorldBindings{
    DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 2, ShaderDescriptorType::SAMPLER, 1},
    DescriptorRequirement{0, 3, ShaderDescriptorType::SAMPLED_IMAGE, 1},
};
struct WorldConstants {
    std::array<int32_t, 2> Origin;
    std::array<uint32_t, 2> CellSize;
    std::array<float, 4> Background;
    uint32_t LayerCount;
    uint32_t Width;
    uint32_t Height;
    uint32_t Reserved = 0;
    uint32_t DisplayMode = 0;
    // Shared body repeat period and CPU-reduced world origin (X,Y). Reducing
    // before float conversion preserves texel phase at distant/negative cells.
    std::array<uint32_t, 3> TerrainTiling{1, 0, 0};
    std::array<float, 4> BackgroundPhases{};
    // displayed image width/height, framebuffer width, active layer count
    std::array<float, 4> BackgroundSize{};
};
static_assert(offsetof(WorldConstants, Background) == 16 && offsetof(WorldConstants, LayerCount) == 32);
static_assert(offsetof(WorldConstants, DisplayMode) == 48);
static_assert(sizeof(WorldConstants) == 96 && sizeof(PostProcessParameters) == 64);

} // namespace
std::unique_ptr<RHIPipeline> WorldPass::CreatePipeline(DynamicRHI &rhi, RHITexture &target) const {
    return rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"), GetShaderInfo("Pixel2DWorldGrid.fs"), WorldBindings, &target, sizeof(WorldConstants)});
}
void WorldPass::Record(RHICommandList &commands, const RenderPassContext &context) const {
    const auto &resources = context.Resources;
    const auto &frame = context.Frame;
    const auto &input = context.Input;
    PIXEL_PROFILE_SCOPE(WorldCommands);
    if (!frame.DirtyRegions.empty()) {
        commands.TransitionTexture(*frame.Data, RHITextureState::TransferDestination);
        for (const auto &region : frame.DirtyRegions)
            commands.CopyBufferToTextureRegion(*frame.Upload, *frame.Data, region);
        commands.TransitionTexture(*frame.Data, RHITextureState::FragmentSampledRead);
    }
    commands.TransitionTexture(*frame.Color, RHITextureState::ColorAttachment);
    commands.BeginColorPass(*frame.Color);
    context.BindFullscreen(commands);
    commands.BindPipeline(*resources.WorldPipeline);
    commands.BindDescriptorSet(*resources.WorldPipeline, *frame.WorldSet, 0);
    auto view = input.View;
    view.OriginPixels = LocalGridPixelOrigin(input);
    WorldConstants constants{view.OriginPixels, view.CellSizePixels, view.Background, context.Catalog.Layers, input.Width, input.Height};
    constants.DisplayMode = uint32_t(input.DisplayMode);
    constants.Reserved = input.MissingOutsideRegion ? 1u : 0u;
    const auto tilePhase = [&](int64_t coordinate) {
        const auto remainder = coordinate % int64_t(context.Catalog.CellsPerTile);
        return uint32_t(remainder < 0 ? remainder + context.Catalog.CellsPerTile : remainder);
    };
    constants.TerrainTiling = {context.Catalog.CellsPerTile, tilePhase(input.CellOrigin[0]), tilePhase(input.CellOrigin[1])};
    if (context.Background && input.DisplayMode == GridDisplayMode::Materials) {
        constants.BackgroundPhases = pixel::BackgroundPhases(*context.Background, input.View, resources.Extent.Width);
        const auto &image = *context.Background->Layers.front().Image;
        const float height = resources.Extent.Height * context.Background->HeightFraction;
        constants.BackgroundSize = {height * image.Width / image.Height, height, float(resources.Extent.Width), float(context.Background->Layers.size())};
    }
    commands.PushConstants(*resources.WorldPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
    commands.DrawIndexed(6);
    commands.EndPass();
    commands.TransitionTexture(*frame.Color, RHITextureState::FragmentSampledRead);
}
} // namespace pixel::render
