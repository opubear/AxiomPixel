#include "PixelRender.h"
#include "Dev/Debug/Performance/FrameProfiler.h"
#include <utility>

PixelRender::PixelRender(DynamicRHI &rhi, GridTextureCatalog catalog,
                         std::function<void(WorldGridFrame &)> input, std::string postShader,
                         std::shared_ptr<const pixel::BackgroundAssets> background,
                         std::shared_ptr<const pixel::CharacterAssets> character,
                         pixel::render::InventoryOverlay::Publisher ui)
    : RHI(rhi), Catalog(std::move(catalog)), Input(std::move(input)), PostShader(std::move(postShader)),
      Background(std::move(background)), CharacterArt(std::move(character)), UIPublisher(std::move(ui)) {
    ValidateGridTextureCatalog(Catalog);
    if (!Input)
        throw std::invalid_argument("2D rendering requires an input provider");
    if (Background)
        pixel::ValidateBackgroundAssets(*Background);
    if (CharacterArt)
        pixel::ValidateCharacterAssets(*CharacterArt);
}
PixelRender::~PixelRender() { Clear(); }
void PixelRender::Initialize() {
    if (!Input)
        throw std::logic_error("cleared 2D policy cannot be reinitialized");
    if (!Resources.Frames.empty())
        throw std::logic_error("2D policy is already initialized");
    if (!RHI.IsPresentationSRGB())
        throw std::runtime_error("2D rendering requires an sRGB presentation target");
    const uint32_t slots = RHI.GetFrameSlotCount();
    if (!slots || slots > UINT32_MAX / 18)
        throw std::runtime_error("invalid RHI frame slot count");
    constexpr std::array<float, 8> vertices{-1, -1, 1, -1, 1, 1, -1, 1};
    constexpr std::array<uint32_t, 6> indices{0, 1, 2, 2, 3, 0};
    Resources.Vertices.reset(RHI.CreateVertexBuffer(2 * sizeof(float), sizeof(vertices), vertices.data()));
    Resources.Indices.reset(RHI.CreateIndexBuffer(sizeof(uint32_t), sizeof(indices), indices.data()));
    Resources.Sampler = RHI.CreateNearestSampler();
    Resources.Materials = RHI.CreateTexture({Catalog.Width, Catalog.Height, Catalog.Layers, RHITextureFormat::RGBA8SRGB, true, false});
    Resources.MaterialUpload = RHI.CreateUploadBuffer(Catalog.Pixels.size());
    Resources.MaterialUpload->Write(Catalog.Pixels);
    if (Background) {
        const auto &image = *Background->Layers.front().Image;
        const size_t layerBytes = image.Pixels.size();
        Resources.BackgroundTexture = RHI.CreateTexture({image.Width, image.Height, uint32_t(Background->Layers.size()), RHITextureFormat::RGBA8SRGB, true, false});
        Resources.BackgroundUpload = RHI.CreateUploadBuffer(layerBytes * Background->Layers.size());
        for (size_t index = 0; index < Background->Layers.size(); ++index)
            Resources.BackgroundUpload->WriteRegion(Background->Layers[index].Image->Pixels, index * layerBytes);
    }
    if (CharacterArt) {
        const auto &image = *CharacterArt->Layers.front();
        Resources.CharacterTexture = RHI.CreateTexture({image.Width, image.Height, uint32_t(CharacterArt->Layers.size()), RHITextureFormat::RGBA8SRGB, true, false});
        Resources.CharacterUpload = RHI.CreateUploadBuffer(image.Pixels.size() * CharacterArt->Layers.size());
        for (size_t index = 0; index < CharacterArt->Layers.size(); ++index)
            Resources.CharacterUpload->WriteRegion(CharacterArt->Layers[index]->Pixels, index * image.Pixels.size());
    }
    Resources.Pool = RHI.CreateImageDescriptorPool(11 * slots, 18 * slots, 5 * slots);
    Resources.Frames.resize(slots);
    for (auto &frame : Resources.Frames) {
        constexpr uint64_t initialBytes = uint64_t(Grid2D::RegionSize) * Grid2D::RegionSize * sizeof(uint32_t);
        frame.Upload = RHI.CreateUploadBuffer(initialBytes);
        frame.WindUpload = RHI.CreateUploadBuffer(initialBytes);
        frame.WindData = RHI.CreateTexture({Grid2D::RegionSize, Grid2D::RegionSize, 1, RHITextureFormat::R32UInt});
        frame.Data = RHI.CreateTexture({Grid2D::RegionSize, Grid2D::RegionSize, 1, RHITextureFormat::R32UInt});
    }
    CreateColorTargets();
    for (auto &frame : Resources.Frames) {
        const std::array bindings{RHIImageBinding{0, frame.Data.get()}, RHIImageBinding{1, Resources.Materials.get()},
                                  RHIImageBinding{2, nullptr, Resources.Sampler.get()},
                                  RHIImageBinding{3, Resources.BackgroundTexture ? Resources.BackgroundTexture.get() : Resources.Materials.get()}};
        frame.WorldSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.WorldPipeline, bindings);
    }
    InventoryUI.Initialize(RHI, std::move(UIPublisher));
    Published = std::make_unique<WorldGridFrame>();
}
void PixelRender::FrameBegin() {
    PIXEL_PROFILE_SCOPE(Publication);
    FrameReady = false;
    if (!Published)
        throw std::logic_error("2D policy must be initialized before publication");
    Input(*Published);
    ValidateWorldGridFrame(Published->Read(), Catalog.Layers, Resources.Extent.Width, Resources.Extent.Height);
    if (Published->Character && Published->Character->Sprite[0] > 0) {
        const auto &draw = *Published->Character;
        if (!CharacterArt || draw.Sprite[0] > CharacterArt->Layers.size() ||
            draw.FrameTexels[0] + draw.FrameTexels[2] > CharacterArt->Layers.front()->Width ||
            draw.FrameTexels[1] + draw.FrameTexels[3] > CharacterArt->Layers.front()->Height)
            throw std::invalid_argument("character frame exceeds loaded art");
    }
    InventoryUI.Prepare(RHI.GetActiveFrameToken().Slot);
    FrameReady = true;
}
void PixelRender::CreateColorTargets() {
    if (!RHI.IsPresentationSRGB())
        throw std::runtime_error("2D presentation format is no longer sRGB");
    Resources.Extent = RHI.GetPresentationExtent();
    if (!Resources.Extent.Width || !Resources.Extent.Height)
        throw std::runtime_error("2D color target requires a drawable extent");
    // Called only at initialization or after GPU idle. Free sets before images/pipeline.
    for (auto &frame : Resources.Frames) {
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
    Resources.PostPipeline.reset();
    for (auto &frame : Resources.Frames) {
        frame.Color = RHI.CreateTexture({Resources.Extent.Width, Resources.Extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
        frame.WindColor = RHI.CreateTexture({Resources.Extent.Width, Resources.Extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
        frame.FormulaColor = RHI.CreateTexture({Resources.Extent.Width, Resources.Extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
        frame.CharacterColor = RHI.CreateTexture({Resources.Extent.Width, Resources.Extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
    }
    if (!Resources.WorldPipeline)
        Resources.WorldPipeline = World.CreatePipeline(RHI, *Resources.Frames.front().Color);
    if (!Resources.WindPipeline)
        Resources.WindPipeline = Wind.CreatePipeline(RHI, *Resources.Frames.front().WindColor);
    if (!Resources.FormulaPipeline)
        Resources.FormulaPipeline = SceneObjects.CreatePipeline(RHI, *Resources.Frames.front().FormulaColor);
    if (!Resources.CharacterPipeline)
        Resources.CharacterPipeline = Character.CreatePipeline(RHI, *Resources.Frames.front().CharacterColor);
    Resources.PostPipeline = PostProcess.CreatePipeline(RHI, PostShader);
    for (auto &frame : Resources.Frames) {
        const std::array bindings{RHIImageBinding{0, frame.Color.get()}, RHIImageBinding{1, nullptr, Resources.Sampler.get()}};
        frame.PostSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.PostPipeline, bindings);
        const std::array characterBindings{RHIImageBinding{0, frame.Color.get()}};
        auto *characterTexture = Resources.CharacterTexture ? Resources.CharacterTexture.get() : Resources.Materials.get();
        const std::array characterArtBindings{RHIImageBinding{0, frame.Color.get()}, RHIImageBinding{1, characterTexture}};
        frame.CharacterSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.CharacterPipeline, characterArtBindings);
        const std::array compositeBindings{RHIImageBinding{0, frame.CharacterColor.get()}, RHIImageBinding{1, nullptr, Resources.Sampler.get()}};
        frame.CharacterPostSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.PostPipeline, compositeBindings);
        const std::array windBindings{RHIImageBinding{0, frame.Color.get()}, RHIImageBinding{1, frame.WindData.get()}};
        frame.WindSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.WindPipeline, windBindings);
        const std::array windPostBindings{RHIImageBinding{0, frame.WindColor.get()}, RHIImageBinding{1, nullptr, Resources.Sampler.get()}};
        frame.WindPostSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.PostPipeline, windPostBindings);
        const std::array windCharacterBindings{RHIImageBinding{0, frame.WindColor.get()}};
        const std::array windCharacterArtBindings{RHIImageBinding{0, frame.WindColor.get()}, RHIImageBinding{1, characterTexture}};
        frame.WindCharacterSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.CharacterPipeline, windCharacterArtBindings);
        frame.FormulaSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.FormulaPipeline, characterBindings);
        frame.WindFormulaSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.FormulaPipeline, windCharacterBindings);
        const std::array formulaBindings{RHIImageBinding{0, frame.FormulaColor.get()}, RHIImageBinding{1, characterTexture}};
        frame.FormulaCharacterSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.CharacterPipeline, formulaBindings);
        const std::array formulaPostBindings{RHIImageBinding{0, frame.FormulaColor.get()}, RHIImageBinding{1, nullptr, Resources.Sampler.get()}};
        frame.FormulaPostSet = RHI.CreateImageDescriptorSet(*Resources.Pool, *Resources.PostPipeline, formulaPostBindings);
    }
}
void PixelRender::OnPresentationChanged(const RHIPresentationChanged &) {
    if (Resources.Frames.empty())
        return;
    RHI.WaitIdle();
    CreateColorTargets();
    InventoryUI.RecreatePipeline(RHI);
}
void PixelRender::Draw() {
    PIXEL_PROFILE_SCOPE(Render);
    if (!FrameReady)
        throw std::logic_error("2D draw requires a successfully published frame");
    FrameReady = false;
    const auto slot = RHI.GetActiveFrameToken().Slot;
    auto &frame = Resources.Frames.at(slot);
    const auto input = Published->Read();
    frame.HasWind = Wind.Enabled(input);
    frame.HasFormula = SceneObjects.Enabled(input);
    frame.HasCharacter = Character.Enabled(input);
    Upload.Prepare(RHI, Resources, frame, input);
    const pixel::render::RenderPassContext context{Resources, frame, input, Catalog, Background.get()};
    RHI.RecordFrame([&](RHICommandList &commands) {
        InventoryUI.Upload(commands, slot);
        Upload.Record(commands, context);
        World.Record(commands, context);
        if (frame.HasWind)
            Wind.Record(commands, context);
        if (frame.HasFormula)
            SceneObjects.Record(commands, context);
        if (frame.HasCharacter)
            Character.Record(commands, context);
        PostProcess.Record(commands, context, [this, slot](RHICommandList &overlay) {
            InventoryUI.Draw(overlay, slot, Resources.Extent);
            RecordOverlay(overlay);
        });
    });
    InventoryUI.Commit(slot);
    WorldUploadBytes += Upload.Commit(frame, input);
    ++FrameCount;
}
const RHITexture &PixelRender::SceneColor(uint32_t slot) const {
    const auto &frame = Resources.Frames.at(slot);
    return frame.HasCharacter ? *frame.CharacterColor : (frame.HasFormula ? *frame.FormulaColor : (frame.HasWind ? *frame.WindColor : *frame.Color));
}
const RHITexture &PixelRender::WorldColor(uint32_t slot) const { return *Resources.Frames.at(slot).Color; }
WorldGridFrameView PixelRender::PublishedFrame() const {
    if (!Published)
        throw std::logic_error("2D policy has no published storage");
    return Published->Read();
}
void PixelRender::Clear() {
    FrameReady = false;
    // Application destroys the game first. Releasing a publisher must never call it.
    Input = {};
    UIPublisher = {};
    InventoryUI.Clear();
    SetOverlayDrawCallback({});
    Published.reset();
    Resources.Frames.clear();
    Resources.Pool.reset();
    Resources.PostPipeline.reset();
    Resources.WorldPipeline.reset();
    Resources.CharacterPipeline.reset();
    Resources.WindPipeline.reset();
    Resources.FormulaPipeline.reset();
    Resources.MaterialUpload.reset();
    Resources.Materials.reset();
    Resources.BackgroundUpload.reset();
    Resources.BackgroundTexture.reset();
    Resources.CharacterUpload.reset();
    Resources.CharacterTexture.reset();
    CharacterArt.reset();
    Background.reset();
    Resources.Sampler.reset();
    Resources.Indices.reset();
    Resources.Vertices.reset();
    Upload.Reset();
    FrameCount = 0;
    WorldUploadBytes = 0;
}
