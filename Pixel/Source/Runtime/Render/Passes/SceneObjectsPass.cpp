#include "Dev/Debug/Performance/FrameProfiler.h"
#include "HAL/Shader/ShaderLibrary.h"
#include "RenderPasses.h"
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pixel::render {
namespace {
struct FormulaConstants {
    std::array<float, 4> CenterRadius{};
    std::array<float, 4> Color{1, 1, 0, 1};
    std::array<uint32_t, 4> Mode{};
    std::array<uint32_t, 4> FrameTexels{};
};
static_assert(sizeof(FormulaConstants) == 64);
constexpr std::array FormulaBindings{DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLED_IMAGE, 1}};

} // namespace
bool SceneObjectsPass::Enabled(const WorldGridFrameView &input) const {
    return (!input.ItemDraws.empty() || !input.FormulaDraws.empty() || !input.DeviceDraws.empty() || !input.GardenDraws.empty() || !input.ActorDraws.empty() || !input.MonsterDraws.empty() || !input.DecorationDraws.empty())
        && input.DisplayMode == GridDisplayMode::Materials;
}
std::unique_ptr<RHIPipeline> SceneObjectsPass::CreatePipeline(DynamicRHI &rhi, RHITexture &target) const {
    return rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"), GetShaderInfo("Pixel2DFormula.fs"), FormulaBindings, &target, sizeof(FormulaConstants)});
}
void SceneObjectsPass::Record(RHICommandList &commands, const RenderPassContext &context) const {
    const auto &resources = context.Resources;
    const auto &frame = context.Frame;
    const auto &input = context.Input;
    commands.TransitionTexture(*frame.FormulaColor, RHITextureState::ColorAttachment);
    commands.BeginColorPass(*frame.FormulaColor);
    context.BindFullscreen(commands);
    commands.BindPipeline(*resources.FormulaPipeline);
    commands.BindDescriptorSet(*resources.FormulaPipeline, frame.HasWind ? *frame.WindFormulaSet : *frame.FormulaSet, 0);
    FormulaConstants constants;
    commands.PushConstants(*resources.FormulaPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
    commands.DrawIndexed(6); // Copy once; subsequent opaque shapes preserve the surrounding scene.
    const auto drawShape = [&](const std::array<float, 4> &g, const std::array<float, 4> &color, uint32_t mode) {
        const float left = std::max(0.f, std::floor(g[0] - g[2]));
        const float top = std::max(0.f, std::floor(g[1] - g[3]));
        const float right = std::min(float(resources.Extent.Width), std::ceil(g[0] + g[2]));
        const float bottom = std::min(float(resources.Extent.Height), std::ceil(g[1] + g[3]));
        if (right <= left || bottom <= top)
            return;
        commands.SetViewport({left, top, right - left, bottom - top, 0, 1});
        commands.SetScissor({int32_t(left), int32_t(top), uint32_t(right - left), uint32_t(bottom - top)});
        constants.CenterRadius = g;
        constants.Color = color;
        constants.Mode[0] = mode;
        commands.PushConstants(*resources.FormulaPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
        commands.DrawIndexed(6);
    };
    for (const auto &draw : input.DecorationDraws)
        drawShape(draw.CenterRadius, draw.Color, 2);
    for (const auto &device : input.DeviceDraws) {
        drawShape(device.CenterRadius, device.Color, 2);
        drawShape(device.OutputCenterRadius, device.OutputColor, 2);
    }
    for (const auto &draw : input.GardenDraws)
        drawShape(draw.CenterRadius, draw.Color, draw.Ellipse ? 1 : 2);
    const auto foreground = [&] {
        for (const auto &draw : input.ItemDraws) drawShape(draw.CenterRadius, draw.Color, 2);
        for (const auto &draw : input.ActorDraws) drawShape(draw.CenterRadius, draw.Color, 2);
        for (const auto &draw : input.FormulaDraws) drawShape(draw.CenterRadius, draw.Color, 1);
    };
    if (input.MonsterDraws.empty()) foreground();
    commands.EndPass();
    commands.TransitionTexture(*frame.FormulaColor, RHITextureState::FragmentSampledRead);

    // The supplied death/hurt sheets contain translucent pixels. Alternate two
    // scene targets to alpha-compose against all preceding objects, including
    // overlapping monsters, without sampling an active color attachment.
    for (size_t index = 0; index < input.MonsterDraws.size(); ++index) {
        const auto &draw = input.MonsterDraws[index];
        auto &target = index % 2 ? *frame.FormulaColor : *frame.MonsterColor;
        const auto &set = index % 2 ? *frame.MonsterToFormulaSet : *frame.FormulaToMonsterSet;
        commands.TransitionTexture(target, RHITextureState::ColorAttachment);
        commands.BeginColorPass(target);
        context.BindFullscreen(commands);
        commands.BindPipeline(*resources.FormulaPipeline);
        commands.BindDescriptorSet(*resources.FormulaPipeline, set, 0);
        const auto &b = draw.Bounds;
        constants.CenterRadius = {b[0] + b[2] * .5f, b[1] + b[3] * .5f, b[2] * .5f, b[3] * .5f};
        constants.FrameTexels = draw.FrameTexels;
        constants.Mode = {3, draw.FacingLeft ? 1u : 0u, 0, 0};
        commands.PushConstants(*resources.FormulaPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
        commands.DrawIndexed(6);
        if (index + 1 == input.MonsterDraws.size() && index % 2) foreground();
        commands.EndPass();
        commands.TransitionTexture(target, RHITextureState::FragmentSampledRead);
    }
    if (input.MonsterDraws.size() % 2) {
        commands.TransitionTexture(*frame.FormulaColor, RHITextureState::ColorAttachment);
        commands.BeginColorPass(*frame.FormulaColor);
        context.BindFullscreen(commands);
        commands.BindPipeline(*resources.FormulaPipeline);
        commands.BindDescriptorSet(*resources.FormulaPipeline, *frame.MonsterToFormulaSet, 0);
        constants.Mode = {};
        commands.PushConstants(*resources.FormulaPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
        commands.DrawIndexed(6);
        foreground();
        commands.EndPass();
        commands.TransitionTexture(*frame.FormulaColor, RHITextureState::FragmentSampledRead);
    }
}
} // namespace pixel::render
