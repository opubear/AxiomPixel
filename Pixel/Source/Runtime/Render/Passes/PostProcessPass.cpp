#include "Dev/Debug/Performance/FrameProfiler.h"
#include "HAL/Shader/ShaderLibrary.h"
#include "RenderPasses.h"

namespace pixel::render {
namespace {
constexpr std::array PostBindings{
    DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLER, 1},
};
constexpr std::array LightPostBindings{
    DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLED_IMAGE, 1},
};
struct LightPostParameters {
    std::array<float, 4> Ambient, Tint, Grid;
};
static_assert(sizeof(LightPostParameters) == 48);

void UploadLightmap(RHICommandList &commands, const LightmapSlot &slot) {
    if (slot.Dirty.empty()) return;
    commands.TransitionTexture(*slot.Texture, RHITextureState::TransferDestination);
    for (const auto &region : slot.Dirty) commands.CopyBufferToTextureRegion(*slot.Upload, *slot.Texture, region);
    commands.TransitionTexture(*slot.Texture, RHITextureState::FragmentSampledRead);
}
} // namespace
std::unique_ptr<RHIPipeline> PostProcessPass::CreatePipeline(DynamicRHI &rhi, const std::string &shader) const {
    return rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"), GetShaderInfo(shader), PostBindings, nullptr, sizeof(PostProcessParameters)});
}
void PostProcessPass::Prepare(DynamicRHI &rhi, RenderResources &resources, FrameResources &frame, const WorldGridFrameView &input) const {
    const auto samples = input.Lights.SamplesPerCell;
    if (!samples) return;
    if (!resources.LightPostPipeline)
        resources.LightPostPipeline = rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"),
            GetShaderInfo("Pixel2DLightPostProcess.fs"), LightPostBindings, nullptr, sizeof(LightPostParameters)});
    const uint32_t width = input.Width * samples, height = input.Height * samples;
    if (!frame.Lights || frame.Lights->Texture->Desc.Width != width || frame.Lights->Texture->Desc.Height != height) {
        auto slot = std::make_unique<LightmapSlot>();
        slot->Texture = rhi.CreateTexture({width, height, 1, RHITextureFormat::RGBA8UNorm});
        slot->Upload = rhi.CreateUploadBuffer(uint64_t(width) * height * sizeof(LightTexel));
        slot->Pool = rhi.CreateImageDescriptorPool(4, 8, 0);
        const std::array scenes{frame.Color.get(), frame.WindColor.get(), frame.FormulaColor.get(), frame.CharacterColor.get()};
        for (size_t i = 0; i < scenes.size(); ++i) {
            const std::array bindings{RHIImageBinding{0, scenes[i]}, RHIImageBinding{1, slot->Texture.get()}};
            slot->Sets[i] = rhi.CreateImageDescriptorSet(*slot->Pool, *resources.LightPostPipeline, bindings);
        }
        frame.Lights = std::move(slot);
    }
    auto &slot = *frame.Lights;
    const auto previous = slot.CellOrigin == input.CellOrigin ? std::span<const uint64_t>(slot.Versions) : std::span<const uint64_t>{};
    StageGridUpload(*slot.Upload, slot.Texture->Desc, std::as_bytes(input.Lights.Texels), Grid2D::RegionSize * samples,
                    input.Lights.RegionVersions, previous, slot.Dirty);
}
void PostProcessPass::Record(RHICommandList &commands, const RenderPassContext &context,
                             const std::function<void(RHICommandList &)> &overlay) const {
    const auto &resources = context.Resources;
    const auto &frame = context.Frame;
    const auto &input = context.Input;
    const bool lit = input.Lights.SamplesPerCell != 0;
    PIXEL_PROFILE_SCOPE(PostCommands);
    if (lit) UploadLightmap(commands, *frame.Lights);
    commands.BeginPresentPass();
    context.BindFullscreen(commands);
    if (lit) {
        const auto origin = LocalGridPixelOrigin(input);
        const auto &color = input.Post.Values;
        const auto &tint = input.Lights.Tint;
        const LightPostParameters parameters{{color[0], color[1], color[2], 1}, {tint[0], tint[1], tint[2], 1},
            {origin[0], origin[1], float(input.Lights.SamplesPerCell) / input.View.CellSizePixels[0], 255.f / LightUnit}};
        commands.BindPipeline(*resources.LightPostPipeline);
        commands.BindDescriptorSet(*resources.LightPostPipeline, *frame.Lights->Sets[frame.SceneIndex()], 0);
        commands.PushConstants(*resources.LightPostPipeline, RHIShaderStage::Fragment, 0, sizeof(parameters), &parameters);
    } else {
        auto parameters = input.Post;
        parameters.Values[PostProcessParameters::TonemappingBypass] = input.DisplayMode == GridDisplayMode::ForceField ? 1.f : 0.f;
        const std::array sets{frame.PostSet.get(), frame.WindPostSet.get(), frame.FormulaPostSet.get(), frame.CharacterPostSet.get()};
        commands.BindPipeline(*resources.PostPipeline);
        commands.BindDescriptorSet(*resources.PostPipeline, *sets[frame.SceneIndex()], 0);
        commands.PushConstants(*resources.PostPipeline, RHIShaderStage::Fragment, 0, sizeof(parameters), &parameters);
    }
    commands.DrawIndexed(6);
    if (overlay)
        overlay(commands);
    commands.EndPass();
}
void PostProcessPass::Commit(FrameResources &frame, const WorldGridFrameView &input) const {
    if (!input.Lights.SamplesPerCell) return;
    frame.Lights->Versions.assign(input.Lights.RegionVersions.begin(), input.Lights.RegionVersions.end());
    frame.Lights->CellOrigin = input.CellOrigin;
}
} // namespace pixel::render
