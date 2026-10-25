#include "Dev/Debug/Performance/FrameProfiler.h"
#include "HAL/Shader/ShaderLibrary.h"
#include "RenderPasses.h"

namespace pixel::render {
namespace {
constexpr std::array PostBindings{
    DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLER, 1},
};
} // namespace
std::unique_ptr<RHIPipeline> PostProcessPass::CreatePipeline(DynamicRHI &rhi, const std::string &shader) const {
    return rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"), GetShaderInfo(shader), PostBindings, nullptr, sizeof(PostProcessParameters)});
}
void PostProcessPass::Record(RHICommandList &commands, const RenderPassContext &context,
                             const std::function<void(RHICommandList &)> &overlay) const {
    const auto &resources = context.Resources;
    const auto &frame = context.Frame;
    const auto &parameters = context.Input.Post;
    PIXEL_PROFILE_SCOPE(PostCommands);
    commands.BeginPresentPass();
    context.BindFullscreen(commands);
    commands.BindPipeline(*resources.PostPipeline);
    commands.BindDescriptorSet(*resources.PostPipeline, frame.HasCharacter ? *frame.CharacterPostSet : (frame.HasFormula ? *frame.FormulaPostSet : (frame.HasWind ? *frame.WindPostSet : *frame.PostSet)), 0);
    commands.PushConstants(*resources.PostPipeline, RHIShaderStage::Fragment, 0, sizeof(parameters), &parameters);
    commands.DrawIndexed(6);
    if (overlay)
        overlay(commands);
    commands.EndPass();
}
} // namespace pixel::render
