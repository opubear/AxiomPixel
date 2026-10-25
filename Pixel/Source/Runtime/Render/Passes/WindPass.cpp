#include "Dev/Debug/Performance/FrameProfiler.h"
#include "HAL/Shader/ShaderLibrary.h"
#include "RenderPasses.h"
#include <cstddef>

namespace pixel::render {
namespace {
constexpr std::array WindBindings{
    DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLED_IMAGE, 1},
};
struct WindConstants {
    std::array<int32_t, 2> Origin;
    std::array<uint32_t, 2> CellSize;
    std::array<uint32_t, 2> Extent;
    uint32_t Mode;
    float Time;
    float Opacity, RefractionPixels, MotionScale;
    float Reserved = 0;
    std::array<uint32_t, 2> GridExtent;
    std::array<uint32_t, 2> NoiseOrigin;
};
static_assert(sizeof(WindConstants) == 64 && offsetof(WindConstants, Opacity) == 32);

} // namespace
bool WindPass::Enabled(const WorldGridFrameView &input) const {
    return input.HasWind && input.Wind.Mode != WindVisualMode::Off && input.Wind.Opacity > 0 && input.DisplayMode == GridDisplayMode::Materials;
}
std::unique_ptr<RHIPipeline> WindPass::CreatePipeline(DynamicRHI &rhi, RHITexture &target) const {
    return rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"), GetShaderInfo("Pixel2DWind.fs"), WindBindings, &target, sizeof(WindConstants)});
}
void WindPass::Record(RHICommandList &commands, const RenderPassContext &context) const {
    const auto &resources = context.Resources;
    const auto &frame = context.Frame;
    const auto &input = context.Input;
    PIXEL_PROFILE_SCOPE(WindCommands);
    if (!frame.WindDirtyRegions.empty()) {
        commands.TransitionTexture(*frame.WindData, RHITextureState::TransferDestination);
        for (const auto &region : frame.WindDirtyRegions)
            commands.CopyBufferToTextureRegion(*frame.WindUpload, *frame.WindData, region);
        commands.TransitionTexture(*frame.WindData, RHITextureState::FragmentSampledRead);
    }
    commands.TransitionTexture(*frame.WindColor, RHITextureState::ColorAttachment);
    commands.BeginColorPass(*frame.WindColor);
    context.BindFullscreen(commands);
    commands.BindPipeline(*resources.WindPipeline);
    commands.BindDescriptorSet(*resources.WindPipeline, *frame.WindSet, 0);
    const auto origin = LocalGridPixelOrigin(input);
    const auto noiseOrigin = [](int64_t coordinate) {
        const auto remainder = coordinate % 1536;
        return uint32_t(remainder < 0 ? remainder + 1536 : remainder);
    };
    const WindConstants constants{origin, input.View.CellSizePixels, {resources.Extent.Width, resources.Extent.Height}, uint32_t(input.Wind.Mode), input.WindTimeSeconds, input.Wind.Opacity, input.Wind.RefractionPixels, input.Wind.MotionScale, 0, {input.Width, input.Height}, {noiseOrigin(input.CellOrigin[0]), noiseOrigin(input.CellOrigin[1])}};
    commands.PushConstants(*resources.WindPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
    commands.DrawIndexed(6);
    commands.EndPass();
    commands.TransitionTexture(*frame.WindColor, RHITextureState::FragmentSampledRead);
}
} // namespace pixel::render
