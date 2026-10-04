#include "PixelRender.h"
#include "HAL/Shader/ShaderLibrary.h"
#include <cstddef>
#include <utility>

namespace {
constexpr std::array WorldBindings{
    DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 2, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 3, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 4, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 5, ShaderDescriptorType::SAMPLER, 1},
};
constexpr std::array PostBindings{
    DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLER, 1},
};
struct WorldConstants {
    std::array<int32_t, 2> Origin;
    std::array<uint32_t, 2> CellSize;
    std::array<float, 4> Background;
    uint32_t LayerCount;
    uint32_t Width = Grid2D::Width;
    uint32_t Height = Grid2D::Height;
    uint32_t PageHeight = Grid2D::PageHeight;
    uint32_t DisplayMode = 0;
    std::array<uint32_t, 3> Reserved{};
};
static_assert(offsetof(WorldConstants, Background) == 16 && offsetof(WorldConstants, LayerCount) == 32);
static_assert(offsetof(WorldConstants, DisplayMode) == 48);
static_assert(sizeof(WorldConstants) == 64 && sizeof(PostProcessParameters) == 64);
} // namespace
PixelRender::PixelRender(DynamicRHI &rhi, GridTextureCatalog catalog,
                         std::function<void(WorldGridFrame &)> input, std::string postShader)
    : RHI(rhi), Catalog(std::move(catalog)), Input(std::move(input)), PostShader(std::move(postShader)) {
    ValidateGridTextureCatalog(Catalog);
    if (!Input)
        throw std::invalid_argument("2D rendering requires an input provider");
}
PixelRender::~PixelRender() { Clear(); }
void PixelRender::Initialize() {
    if (!Input)
        throw std::logic_error("cleared 2D policy cannot be reinitialized");
    if (!Frames.empty())
        throw std::logic_error("2D policy is already initialized");
    if (!RHI.IsPresentationSRGB())
        throw std::runtime_error("2D rendering requires an sRGB presentation target");
    const uint32_t slots = RHI.GetFrameSlotCount();
    if (!slots || slots > UINT32_MAX / 6)
        throw std::runtime_error("invalid RHI frame slot count");
    constexpr std::array<float, 8> vertices{-1, -1, 1, -1, 1, 1, -1, 1};
    constexpr std::array<uint32_t, 6> indices{0, 1, 2, 2, 3, 0};
    Vertices.reset(RHI.CreateVertexBuffer(2 * sizeof(float), sizeof(vertices), vertices.data()));
    Indices.reset(RHI.CreateIndexBuffer(sizeof(uint32_t), sizeof(indices), indices.data()));
    Sampler = RHI.CreateNearestSampler();
    Materials = RHI.CreateTexture({Catalog.Width, Catalog.Height, Catalog.Layers, RHITextureFormat::RGBA8SRGB, true, false});
    MaterialUpload = RHI.CreateUploadBuffer(Catalog.Pixels.size());
    MaterialUpload->Write(Catalog.Pixels);
    Pool = RHI.CreateImageDescriptorPool(2 * slots, 6 * slots, 2 * slots);
    Frames.resize(slots);
    for (auto &frame : Frames) {
        frame.Upload = RHI.CreateUploadBuffer(Grid2D::UploadBytes);
        for (auto &page : frame.Data)
            page = RHI.CreateTexture({Grid2D::Width, Grid2D::PageHeight, 1, RHITextureFormat::R32UInt});
    }
    CreateColorTargets();
    for (auto &frame : Frames) {
        const std::array bindings{
            RHIImageBinding{0, frame.Data[0].get()}, RHIImageBinding{1, frame.Data[1].get()},
            RHIImageBinding{2, frame.Data[2].get()}, RHIImageBinding{3, frame.Data[3].get()},
            RHIImageBinding{4, Materials.get()}, RHIImageBinding{5, nullptr, Sampler.get()}};
        frame.WorldSet = RHI.CreateImageDescriptorSet(*Pool, *WorldPipeline, bindings);
    }
    Published = std::make_unique<WorldGridFrame>();
}
void PixelRender::FrameBegin() {
    FrameReady = false;
    if (!Published)
        throw std::logic_error("2D policy must be initialized before publication");
    Input(*Published);
    ValidateWorldGridFrame(Published->Read(), Catalog.Layers, Extent.Width, Extent.Height);
    FrameReady = true;
}
void PixelRender::CreateColorTargets() {
    if (!RHI.IsPresentationSRGB())
        throw std::runtime_error("2D presentation format is no longer sRGB");
    Extent = RHI.GetPresentationExtent();
    if (!Extent.Width || !Extent.Height)
        throw std::runtime_error("2D color target requires a drawable extent");
    // Called only at initialization or after GPU idle. Free sets before images/pipeline.
    for (auto &frame : Frames)
        frame.PostSet.reset();
    PostPipeline.reset();
    for (auto &frame : Frames)
        frame.Color = RHI.CreateTexture({Extent.Width, Extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
    const auto &vertex = GetShaderInfo("Pixel2DFullscreen.vs");
    if (!WorldPipeline)
        WorldPipeline = RHI.CreateRasterPipeline({vertex, GetShaderInfo("Pixel2DWorldGrid.fs"), WorldBindings, Frames.front().Color.get(), sizeof(WorldConstants)});
    PostPipeline = RHI.CreateRasterPipeline({vertex, GetShaderInfo(PostShader), PostBindings, nullptr, sizeof(PostProcessParameters)});
    for (auto &frame : Frames) {
        const std::array bindings{RHIImageBinding{0, frame.Color.get()}, RHIImageBinding{1, nullptr, Sampler.get()}};
        frame.PostSet = RHI.CreateImageDescriptorSet(*Pool, *PostPipeline, bindings);
    }
}
void PixelRender::OnPresentationChanged() {
    if (Frames.empty())
        return;
    RHI.WaitIdle();
    CreateColorTargets();
}
void PixelRender::SetFullViewport(RHICommandList &commands) {
    commands.SetViewport({0, 0, float(Extent.Width), float(Extent.Height), 0, 1});
    commands.SetScissor({0, 0, Extent.Width, Extent.Height});
    commands.BindVertexBuffer(*Vertices);
    commands.BindIndexBuffer(*Indices);
}
void PixelRender::RecordWorld(RHICommandList &commands, FrameResources &frame, const GridView2D &view, GridDisplayMode mode) {
    for (uint32_t page = 0; page < Grid2D::PageCount; ++page) {
        commands.TransitionTexture(*frame.Data[page], RHITextureState::TransferDestination);
        commands.CopyBufferToTexture(*frame.Upload, *frame.Data[page], page * Grid2D::PageBytes);
        commands.TransitionTexture(*frame.Data[page], RHITextureState::FragmentSampledRead);
    }
    commands.TransitionTexture(*frame.Color, RHITextureState::ColorAttachment);
    commands.BeginColorPass(*frame.Color);
    SetFullViewport(commands);
    commands.BindPipeline(*WorldPipeline);
    commands.BindDescriptorSet(*WorldPipeline, *frame.WorldSet, 0);
    WorldConstants constants{view.OriginPixels, view.CellSizePixels, view.Background, Catalog.Layers};
    constants.DisplayMode = uint32_t(mode);
    commands.PushConstants(*WorldPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
    commands.DrawIndexed(6);
    commands.EndPass();
    commands.TransitionTexture(*frame.Color, RHITextureState::FragmentSampledRead);
}
void PixelRender::RecordPost(RHICommandList &commands, FrameResources &frame, const PostProcessParameters &parameters) {
    commands.BeginPresentPass();
    SetFullViewport(commands);
    commands.BindPipeline(*PostPipeline);
    commands.BindDescriptorSet(*PostPipeline, *frame.PostSet, 0);
    commands.PushConstants(*PostPipeline, RHIShaderStage::Fragment, 0, sizeof(parameters), &parameters);
    commands.DrawIndexed(6);
    if (Overlay)
        Overlay(commands);
    commands.EndPass();
}
void PixelRender::Draw() {
    if (!FrameReady)
        throw std::logic_error("2D draw requires a successfully published frame");
    FrameReady = false;
    const auto token = RHI.GetActiveFrameToken();
    auto &frame = Frames.at(token.Slot);
    const auto input = Published->Read();
    frame.Upload->Write(std::as_bytes(input.Cells));
    RHI.RecordFrame([&](RHICommandList &commands) {
        if (!MaterialsUploaded) {
            commands.TransitionTexture(*Materials, RHITextureState::TransferDestination);
            commands.CopyBufferToTexture(*MaterialUpload, *Materials);
            commands.TransitionTexture(*Materials, RHITextureState::FragmentSampledRead);
        }
        RecordWorld(commands, frame, input.View, input.DisplayMode);
        RecordPost(commands, frame, input.Post);
    });
    MaterialsUploaded = true;
    ++FrameCount;
}
const RHITexture &PixelRender::SceneColor(uint32_t slot) const { return *Frames.at(slot).Color; }
WorldGridFrameView PixelRender::PublishedFrame() const {
    if (!Published)
        throw std::logic_error("2D policy has no published storage");
    return Published->Read();
}
void PixelRender::Clear() {
    FrameReady = false;
    // Engine destroys the game first. Releasing a publisher must never call it.
    Input = {};
    Overlay = {};
    Published.reset();
    Frames.clear();
    Pool.reset();
    PostPipeline.reset();
    WorldPipeline.reset();
    MaterialUpload.reset();
    Materials.reset();
    Sampler.reset();
    Indices.reset();
    Vertices.reset();
    MaterialsUploaded = false;
    FrameCount = 0;
}
