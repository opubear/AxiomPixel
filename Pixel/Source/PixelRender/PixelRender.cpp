#include "PixelRender.h"
#include "BackgroundView.h"
#include "HAL/Shader/ShaderLibrary.h"
#include "Performance/FrameProfiler.h"
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr std::array WorldBindings{
    DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 2, ShaderDescriptorType::SAMPLER, 1},
    DescriptorRequirement{0, 3, ShaderDescriptorType::SAMPLED_IMAGE, 1},
};
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
struct FormulaConstants {
    std::array<float, 4> CenterRadius{};
    std::array<float, 4> Color{1, 1, 0, 1};
    std::array<uint32_t, 4> Mode{};
};
static_assert(sizeof(FormulaConstants) == 48);
constexpr std::array FormulaBindings{DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1}};
constexpr std::array CharacterBindings{DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1}};
constexpr std::array PostBindings{
    DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1},
    DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLER, 1},
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
PixelRender::PixelRender(DynamicRHI &rhi, GridTextureCatalog catalog,
                         std::function<void(WorldGridFrame &)> input, std::string postShader,
                         std::shared_ptr<const pixel::BackgroundAssets> background)
    : RHI(rhi), Catalog(std::move(catalog)), Input(std::move(input)), PostShader(std::move(postShader)),
      Background(std::move(background)) {
    ValidateGridTextureCatalog(Catalog);
    if (!Input)
        throw std::invalid_argument("2D rendering requires an input provider");
    if (Background)
        pixel::ValidateBackgroundAssets(*Background);
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
    if (!slots || slots > UINT32_MAX / 15)
        throw std::runtime_error("invalid RHI frame slot count");
    constexpr std::array<float, 8> vertices{-1, -1, 1, -1, 1, 1, -1, 1};
    constexpr std::array<uint32_t, 6> indices{0, 1, 2, 2, 3, 0};
    Vertices.reset(RHI.CreateVertexBuffer(2 * sizeof(float), sizeof(vertices), vertices.data()));
    Indices.reset(RHI.CreateIndexBuffer(sizeof(uint32_t), sizeof(indices), indices.data()));
    Sampler = RHI.CreateNearestSampler();
    Materials = RHI.CreateTexture({Catalog.Width, Catalog.Height, Catalog.Layers, RHITextureFormat::RGBA8SRGB, true, false});
    MaterialUpload = RHI.CreateUploadBuffer(Catalog.Pixels.size());
    MaterialUpload->Write(Catalog.Pixels);
    if (Background) {
        const auto &image = *Background->Layers.front().Image;
        const size_t layerBytes = image.Pixels.size();
        BackgroundTexture = RHI.CreateTexture({image.Width, image.Height, uint32_t(Background->Layers.size()), RHITextureFormat::RGBA8SRGB, true, false});
        BackgroundUpload = RHI.CreateUploadBuffer(layerBytes * Background->Layers.size());
        for (size_t index = 0; index < Background->Layers.size(); ++index)
            BackgroundUpload->WriteRegion(Background->Layers[index].Image->Pixels, index * layerBytes);
    }
    Pool = RHI.CreateImageDescriptorPool(11 * slots, 15 * slots, 5 * slots);
    Frames.resize(slots);
    for (auto &frame : Frames) {
        constexpr uint64_t initialBytes = uint64_t(Grid2D::RegionSize) * Grid2D::RegionSize * sizeof(uint32_t);
        frame.Upload = RHI.CreateUploadBuffer(initialBytes);
        frame.WindUpload = RHI.CreateUploadBuffer(initialBytes);
        frame.WindData = RHI.CreateTexture({Grid2D::RegionSize, Grid2D::RegionSize, 1, RHITextureFormat::R32UInt});
        frame.Data = RHI.CreateTexture({Grid2D::RegionSize, Grid2D::RegionSize, 1, RHITextureFormat::R32UInt});
    }
    CreateColorTargets();
    for (auto &frame : Frames) {
        const std::array bindings{RHIImageBinding{0, frame.Data.get()}, RHIImageBinding{1, Materials.get()},
                                  RHIImageBinding{2, nullptr, Sampler.get()},
                                  RHIImageBinding{3, BackgroundTexture ? BackgroundTexture.get() : Materials.get()}};
        frame.WorldSet = RHI.CreateImageDescriptorSet(*Pool, *WorldPipeline, bindings);
    }
    Published = std::make_unique<WorldGridFrame>();
}
void PixelRender::FrameBegin() {
    PIXEL_PROFILE_SCOPE(Publication);
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
    for (auto &frame : Frames) {
        frame.PostSet.reset();
        frame.CharacterSet.reset();
        frame.CharacterPostSet.reset();
        frame.WindSet.reset();
        frame.WindPostSet.reset();
        frame.WindCharacterSet.reset();
        frame.FormulaSet.reset();
        frame.WindFormulaSet.reset();
        frame.FormulaPostSet.reset();
        frame.FormulaCharacterSet.reset();
        frame.HasFormula = false;
        frame.HasWind = false;
        frame.HasCharacter = false;
        frame.RegionVersions.clear();
        frame.WindRegionVersions.clear();
    }
    PostPipeline.reset();
    for (auto &frame : Frames) {
        frame.Color = RHI.CreateTexture({Extent.Width, Extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
        frame.WindColor = RHI.CreateTexture({Extent.Width, Extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
        frame.FormulaColor = RHI.CreateTexture({Extent.Width, Extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
        frame.CharacterColor = RHI.CreateTexture({Extent.Width, Extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
    }
    const auto &vertex = GetShaderInfo("Pixel2DFullscreen.vs");
    if (!WorldPipeline)
        WorldPipeline = RHI.CreateRasterPipeline({vertex, GetShaderInfo("Pixel2DWorldGrid.fs"), WorldBindings, Frames.front().Color.get(), sizeof(WorldConstants)});
    if (!WindPipeline)
        WindPipeline = RHI.CreateRasterPipeline({vertex, GetShaderInfo("Pixel2DWind.fs"), WindBindings, Frames.front().WindColor.get(), sizeof(WindConstants)});
    if (!FormulaPipeline)
        FormulaPipeline = RHI.CreateRasterPipeline({vertex, GetShaderInfo("Pixel2DFormula.fs"), FormulaBindings, Frames.front().FormulaColor.get(), sizeof(FormulaConstants)});
    if (!CharacterPipeline)
        CharacterPipeline = RHI.CreateRasterPipeline({vertex, GetShaderInfo("Pixel2DCharacter.fs"), CharacterBindings, Frames.front().CharacterColor.get(), sizeof(CharacterDraw)});
    PostPipeline = RHI.CreateRasterPipeline({vertex, GetShaderInfo(PostShader), PostBindings, nullptr, sizeof(PostProcessParameters)});
    for (auto &frame : Frames) {
        const std::array bindings{RHIImageBinding{0, frame.Color.get()}, RHIImageBinding{1, nullptr, Sampler.get()}};
        frame.PostSet = RHI.CreateImageDescriptorSet(*Pool, *PostPipeline, bindings);
        const std::array characterBindings{RHIImageBinding{0, frame.Color.get()}};
        frame.CharacterSet = RHI.CreateImageDescriptorSet(*Pool, *CharacterPipeline, characterBindings);
        const std::array compositeBindings{RHIImageBinding{0, frame.CharacterColor.get()}, RHIImageBinding{1, nullptr, Sampler.get()}};
        frame.CharacterPostSet = RHI.CreateImageDescriptorSet(*Pool, *PostPipeline, compositeBindings);
        const std::array windBindings{RHIImageBinding{0, frame.Color.get()}, RHIImageBinding{1, frame.WindData.get()}};
        frame.WindSet = RHI.CreateImageDescriptorSet(*Pool, *WindPipeline, windBindings);
        const std::array windPostBindings{RHIImageBinding{0, frame.WindColor.get()}, RHIImageBinding{1, nullptr, Sampler.get()}};
        frame.WindPostSet = RHI.CreateImageDescriptorSet(*Pool, *PostPipeline, windPostBindings);
        const std::array windCharacterBindings{RHIImageBinding{0, frame.WindColor.get()}};
        frame.WindCharacterSet = RHI.CreateImageDescriptorSet(*Pool, *CharacterPipeline, windCharacterBindings);
        frame.FormulaSet = RHI.CreateImageDescriptorSet(*Pool, *FormulaPipeline, characterBindings);
        frame.WindFormulaSet = RHI.CreateImageDescriptorSet(*Pool, *FormulaPipeline, windCharacterBindings);
        const std::array formulaBindings{RHIImageBinding{0, frame.FormulaColor.get()}};
        frame.FormulaCharacterSet = RHI.CreateImageDescriptorSet(*Pool, *CharacterPipeline, formulaBindings);
        const std::array formulaPostBindings{RHIImageBinding{0, frame.FormulaColor.get()}, RHIImageBinding{1, nullptr, Sampler.get()}};
        frame.FormulaPostSet = RHI.CreateImageDescriptorSet(*Pool, *PostPipeline, formulaPostBindings);
    }
}
void PixelRender::OnPresentationChanged(const RHIPresentationChanged &) {
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
void PixelRender::CreateGridResources(FrameResources &frame, uint32_t width, uint32_t height) {
    // The active frame slot has completed on the GPU. Other slots keep their
    // descriptors and images until their own next use.
    frame.WorldSet.reset();
    frame.WindSet.reset();
    frame.Data = RHI.CreateTexture({width, height, 1, RHITextureFormat::R32UInt});
    frame.WindData = RHI.CreateTexture({width, height, 1, RHITextureFormat::R32UInt});
    const size_t bytes = size_t(width) * height * sizeof(uint32_t);
    frame.Upload = RHI.CreateUploadBuffer(bytes);
    frame.WindUpload = RHI.CreateUploadBuffer(bytes);
    const std::array bindings{RHIImageBinding{0, frame.Data.get()}, RHIImageBinding{1, Materials.get()},
                              RHIImageBinding{2, nullptr, Sampler.get()},
                              RHIImageBinding{3, BackgroundTexture ? BackgroundTexture.get() : Materials.get()}};
    frame.WorldSet = RHI.CreateImageDescriptorSet(*Pool, *WorldPipeline, bindings);
    const std::array windBindings{RHIImageBinding{0, frame.Color.get()}, RHIImageBinding{1, frame.WindData.get()}};
    frame.WindSet = RHI.CreateImageDescriptorSet(*Pool, *WindPipeline, windBindings);
    frame.RegionVersions.clear();
    frame.WindRegionVersions.clear();
}
void PixelRender::PrepareGridUpload(FrameResources &frame, const WorldGridFrameView &input) {
    if (frame.Data->Desc.Width != input.Width || frame.Data->Desc.Height != input.Height)
        CreateGridResources(frame, input.Width, input.Height);
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
void PixelRender::RecordWorld(RHICommandList &commands, FrameResources &frame, const WorldGridFrameView &input) {
    PIXEL_PROFILE_SCOPE(WorldCommands);
    if (!frame.DirtyRegions.empty()) {
        commands.TransitionTexture(*frame.Data, RHITextureState::TransferDestination);
        for (const auto &region : frame.DirtyRegions)
            commands.CopyBufferToTextureRegion(*frame.Upload, *frame.Data, region);
        commands.TransitionTexture(*frame.Data, RHITextureState::FragmentSampledRead);
    }
    commands.TransitionTexture(*frame.Color, RHITextureState::ColorAttachment);
    commands.BeginColorPass(*frame.Color);
    SetFullViewport(commands);
    commands.BindPipeline(*WorldPipeline);
    commands.BindDescriptorSet(*WorldPipeline, *frame.WorldSet, 0);
    auto view = input.View;
    view.OriginPixels = LocalGridPixelOrigin(input);
    WorldConstants constants{view.OriginPixels, view.CellSizePixels, view.Background, Catalog.Layers, input.Width, input.Height};
    constants.DisplayMode = uint32_t(input.DisplayMode);
    constants.Reserved = input.MissingOutsideRegion ? 1u : 0u;
    const auto tilePhase = [&](int64_t coordinate) {
        const auto remainder = coordinate % int64_t(Catalog.CellsPerTile);
        return uint32_t(remainder < 0 ? remainder + Catalog.CellsPerTile : remainder);
    };
    constants.TerrainTiling = {Catalog.CellsPerTile, tilePhase(input.CellOrigin[0]), tilePhase(input.CellOrigin[1])};
    if (Background && input.DisplayMode == GridDisplayMode::Materials) {
        constants.BackgroundPhases = pixel::BackgroundPhases(*Background, input.View, Extent.Width);
        const auto &image = *Background->Layers.front().Image;
        const float height = Extent.Height * Background->HeightFraction;
        constants.BackgroundSize = {height * image.Width / image.Height, height, float(Extent.Width), float(Background->Layers.size())};
    }
    commands.PushConstants(*WorldPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
    commands.DrawIndexed(6);
    commands.EndPass();
    commands.TransitionTexture(*frame.Color, RHITextureState::FragmentSampledRead);
}
void PixelRender::RecordWind(RHICommandList &commands, FrameResources &frame, const WorldGridFrameView &input) {
    PIXEL_PROFILE_SCOPE(WindCommands);
    if (!frame.WindDirtyRegions.empty()) {
        commands.TransitionTexture(*frame.WindData, RHITextureState::TransferDestination);
        for (const auto &region : frame.WindDirtyRegions)
            commands.CopyBufferToTextureRegion(*frame.WindUpload, *frame.WindData, region);
        commands.TransitionTexture(*frame.WindData, RHITextureState::FragmentSampledRead);
    }
    commands.TransitionTexture(*frame.WindColor, RHITextureState::ColorAttachment);
    commands.BeginColorPass(*frame.WindColor);
    SetFullViewport(commands);
    commands.BindPipeline(*WindPipeline);
    commands.BindDescriptorSet(*WindPipeline, *frame.WindSet, 0);
    const auto origin = LocalGridPixelOrigin(input);
    const auto noiseOrigin = [](int64_t coordinate) {
        const auto remainder = coordinate % 1536;
        return uint32_t(remainder < 0 ? remainder + 1536 : remainder);
    };
    const WindConstants constants{origin, input.View.CellSizePixels, {Extent.Width, Extent.Height}, uint32_t(input.Wind.Mode), input.WindTimeSeconds, input.Wind.Opacity, input.Wind.RefractionPixels, input.Wind.MotionScale, 0, {input.Width, input.Height}, {noiseOrigin(input.CellOrigin[0]), noiseOrigin(input.CellOrigin[1])}};
    commands.PushConstants(*WindPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
    commands.DrawIndexed(6);
    commands.EndPass();
    commands.TransitionTexture(*frame.WindColor, RHITextureState::FragmentSampledRead);
}
void PixelRender::RecordFormula(RHICommandList &commands, FrameResources &frame, const WorldGridFrameView &input) {
    commands.TransitionTexture(*frame.FormulaColor, RHITextureState::ColorAttachment);
    commands.BeginColorPass(*frame.FormulaColor);
    SetFullViewport(commands);
    commands.BindPipeline(*FormulaPipeline);
    commands.BindDescriptorSet(*FormulaPipeline, frame.HasWind ? *frame.WindFormulaSet : *frame.FormulaSet, 0);
    FormulaConstants constants;
    commands.PushConstants(*FormulaPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
    commands.DrawIndexed(6); // Copy once; subsequent opaque shapes preserve the surrounding scene.
    const auto drawShape = [&](const std::array<float, 4> &g, const std::array<float, 4> &color, uint32_t mode) {
        const float left = std::max(0.f, std::floor(g[0] - g[2]));
        const float top = std::max(0.f, std::floor(g[1] - g[3]));
        const float right = std::min(float(Extent.Width), std::ceil(g[0] + g[2]));
        const float bottom = std::min(float(Extent.Height), std::ceil(g[1] + g[3]));
        if (right <= left || bottom <= top)
            return;
        commands.SetViewport({left, top, right - left, bottom - top, 0, 1});
        commands.SetScissor({int32_t(left), int32_t(top), uint32_t(right - left), uint32_t(bottom - top)});
        constants.CenterRadius = g;
        constants.Color = color;
        constants.Mode[0] = mode;
        commands.PushConstants(*FormulaPipeline, RHIShaderStage::Fragment, 0, sizeof(constants), &constants);
        commands.DrawIndexed(6);
    };
    for (const auto &device : input.DeviceDraws) {
        drawShape(device.CenterRadius, device.Color, 2);
        drawShape(device.OutputCenterRadius, device.OutputColor, 2);
    }
    for (const auto &draw : input.FormulaDraws)
        drawShape(draw.CenterRadius, draw.Color, 1);
    commands.EndPass();
    commands.TransitionTexture(*frame.FormulaColor, RHITextureState::FragmentSampledRead);
}
void PixelRender::RecordCharacter(RHICommandList &commands, FrameResources &frame, const CharacterDraw &character) {
    PIXEL_PROFILE_SCOPE(CharacterCommands);
    commands.TransitionTexture(*frame.CharacterColor, RHITextureState::ColorAttachment);
    commands.BeginColorPass(*frame.CharacterColor);
    SetFullViewport(commands);
    commands.BindPipeline(*CharacterPipeline);
    commands.BindDescriptorSet(*CharacterPipeline, frame.HasFormula ? *frame.FormulaCharacterSet : (frame.HasWind ? *frame.WindCharacterSet : *frame.CharacterSet), 0);
    commands.PushConstants(*CharacterPipeline, RHIShaderStage::Fragment, 0, sizeof(character), &character);
    commands.DrawIndexed(6);
    commands.EndPass();
    commands.TransitionTexture(*frame.CharacterColor, RHITextureState::FragmentSampledRead);
}
void PixelRender::RecordPost(RHICommandList &commands, FrameResources &frame, const PostProcessParameters &parameters) {
    PIXEL_PROFILE_SCOPE(PostCommands);
    commands.BeginPresentPass();
    SetFullViewport(commands);
    commands.BindPipeline(*PostPipeline);
    commands.BindDescriptorSet(*PostPipeline, frame.HasCharacter ? *frame.CharacterPostSet : (frame.HasFormula ? *frame.FormulaPostSet : (frame.HasWind ? *frame.WindPostSet : *frame.PostSet)), 0);
    commands.PushConstants(*PostPipeline, RHIShaderStage::Fragment, 0, sizeof(parameters), &parameters);
    commands.DrawIndexed(6);
    RecordOverlay(commands);
    commands.EndPass();
}
void PixelRender::Draw() {
    PIXEL_PROFILE_SCOPE(Render);
    if (!FrameReady)
        throw std::logic_error("2D draw requires a successfully published frame");
    FrameReady = false;
    const auto token = RHI.GetActiveFrameToken();
    auto &frame = Frames.at(token.Slot);
    const auto input = Published->Read();
    frame.HasWind = input.HasWind && input.Wind.Mode != WindVisualMode::Off && input.Wind.Opacity > 0 && input.DisplayMode == GridDisplayMode::Materials;
    {
        PIXEL_PROFILE_SCOPE(Upload);
        PrepareGridUpload(frame, input);
    }
    frame.HasFormula = (!input.FormulaDraws.empty() || !input.DeviceDraws.empty()) && input.DisplayMode == GridDisplayMode::Materials;
    frame.HasCharacter = input.Character.has_value() && input.DisplayMode == GridDisplayMode::Materials;
    RHI.RecordFrame([&](RHICommandList &commands) {
        if (!MaterialsUploaded) {
            commands.TransitionTexture(*Materials, RHITextureState::TransferDestination);
            commands.CopyBufferToTexture(*MaterialUpload, *Materials);
            commands.TransitionTexture(*Materials, RHITextureState::FragmentSampledRead);
            if (BackgroundTexture) {
                commands.TransitionTexture(*BackgroundTexture, RHITextureState::TransferDestination);
                commands.CopyBufferToTexture(*BackgroundUpload, *BackgroundTexture);
                commands.TransitionTexture(*BackgroundTexture, RHITextureState::FragmentSampledRead);
            }
        }
        RecordWorld(commands, frame, input);
        if (frame.HasWind)
            RecordWind(commands, frame, input);
        if (frame.HasFormula)
            RecordFormula(commands, frame, input);
        if (frame.HasCharacter)
            RecordCharacter(commands, frame, *input.Character);
        RecordPost(commands, frame, input.Post);
    });
    MaterialsUploaded = true;
    frame.RegionVersions.assign(input.RegionVersions.begin(), input.RegionVersions.end());
    if (frame.HasWind)
        frame.WindRegionVersions.assign(input.WindRegionVersions.begin(), input.WindRegionVersions.end());
    frame.CellOrigin = input.CellOrigin;
    frame.DisplayMode = input.DisplayMode;
    for (const auto &region : frame.DirtyRegions)
        WorldUploadBytes += uint64_t(region.Width) * region.Height * sizeof(uint32_t);
    ++FrameCount;
}
const RHITexture &PixelRender::SceneColor(uint32_t slot) const {
    const auto &frame = Frames.at(slot);
    return frame.HasCharacter ? *frame.CharacterColor : (frame.HasFormula ? *frame.FormulaColor : (frame.HasWind ? *frame.WindColor : *frame.Color));
}
const RHITexture &PixelRender::WorldColor(uint32_t slot) const { return *Frames.at(slot).Color; }
WorldGridFrameView PixelRender::PublishedFrame() const {
    if (!Published)
        throw std::logic_error("2D policy has no published storage");
    return Published->Read();
}
void PixelRender::Clear() {
    FrameReady = false;
    // Application destroys the game first. Releasing a publisher must never call it.
    Input = {};
    SetOverlayDrawCallback({});
    Published.reset();
    Frames.clear();
    Pool.reset();
    PostPipeline.reset();
    WorldPipeline.reset();
    CharacterPipeline.reset();
    WindPipeline.reset();
    FormulaPipeline.reset();
    MaterialUpload.reset();
    Materials.reset();
    BackgroundUpload.reset();
    BackgroundTexture.reset();
    Background.reset();
    Sampler.reset();
    Indices.reset();
    Vertices.reset();
    MaterialsUploaded = false;
    FrameCount = 0;
    WorldUploadBytes = 0;
}
