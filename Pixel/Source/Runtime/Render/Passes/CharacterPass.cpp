#include "Dev/Debug/Performance/FrameProfiler.h"
#include "HAL/Shader/ShaderLibrary.h"
#include "RenderPasses.h"

namespace pixel::render {
namespace {
constexpr std::array CharacterBindings{DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
                                      DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLED_IMAGE, 1}};

} // namespace
bool CharacterPass::Enabled(const WorldGridFrameView &input) const {
    return input.Character.has_value() && input.DisplayMode == GridDisplayMode::Materials;
}
std::unique_ptr<RHIPipeline> CharacterPass::CreatePipeline(DynamicRHI &rhi, RHITexture &target) const {
    return rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"), GetShaderInfo("Pixel2DCharacter.fs"), CharacterBindings, &target, sizeof(CharacterDraw)});
}
void CharacterPass::Record(RHICommandList &commands, const RenderPassContext &context) const {
    const auto &resources = context.Resources;
    const auto &frame = context.Frame;
    const auto &character = *context.Input.Character;
    PIXEL_PROFILE_SCOPE(CharacterCommands);
    commands.TransitionTexture(*frame.CharacterColor, RHITextureState::ColorAttachment);
    commands.BeginColorPass(*frame.CharacterColor);
    context.BindFullscreen(commands);
    commands.BindPipeline(*resources.CharacterPipeline);
    commands.BindDescriptorSet(*resources.CharacterPipeline, frame.HasFormula ? *frame.FormulaCharacterSet : (frame.HasWind ? *frame.WindCharacterSet : *frame.CharacterSet), 0);
    commands.PushConstants(*resources.CharacterPipeline, RHIShaderStage::Fragment, 0, sizeof(character), &character);
    commands.DrawIndexed(6);
    commands.EndPass();
    commands.TransitionTexture(*frame.CharacterColor, RHITextureState::FragmentSampledRead);
}
} // namespace pixel::render
