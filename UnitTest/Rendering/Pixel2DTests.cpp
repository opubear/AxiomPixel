#include "HAL/Shader/ShaderLibrary.h"
#include "Runtime/Render/PixelRender.h"
#include "Runtime/Render/PixelWorldPresentation.h"
#include "Runtime/Simulation/World/World.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

void RunPixelWorldTests();
void RunPixelFieldTests();
void RunPixelWindTests();
void RunPixelFieldViewTests();
void RunPixelDirtWindTests();
void RunPixelWindVisualTests();

namespace {
void Check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
template <class F>
void Reject(F &&action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }
    Check(rejected, "expected invalid input to be rejected");
}
struct MockUpload : RHIUploadBuffer {
    static inline size_t Writes = 0;
    std::vector<std::byte> Bytes;
    explicit MockUpload(size_t size) : Bytes(size) {}
    void Write(std::span<const std::byte> bytes) override {
        ++Writes;
        Check(bytes.size() == Bytes.size(), "partial upload");
        std::copy(bytes.begin(), bytes.end(), Bytes.begin());
    }
    void WriteRegion(std::span<const std::byte> bytes, uint64_t offset) override {
        ++Writes;
        Check(offset <= Bytes.size() && bytes.size() <= Bytes.size() - offset, "partial write outside upload");
        std::copy(bytes.begin(), bytes.end(), Bytes.begin() + offset);
    }
};
struct MockImage : RHITexture {
    using RHITexture::RHITexture;
};
struct MockCommands : RHICommandList {
    RHIFrameToken Token{};
    std::vector<std::string> Events;
    std::vector<std::pair<const RHITexture *, std::vector<std::byte>>> Copies;
    std::vector<std::vector<std::byte>> Constants;
    std::vector<RHITextureCopyRegion> Regions;
    bool InPass = false;
    const RHIFrameToken &GetFrameToken() const override { return Token; }
    void BindPipeline(const RHIPipeline &) override { Check(InPass, "pipeline outside pass"); }
    void BindDescriptorSet(const RHIPipeline &, const RHIDescriptorSet &, uint32_t) override {}
    void BindVertexBuffer(const RHIBuffer &, uint32_t, uint64_t) override {}
    void BindIndexBuffer(const RHIBuffer &, RHIIndexType, uint64_t) override {}
    void DrawIndexed(uint32_t count, uint32_t instances, uint32_t, int32_t, uint32_t) override {
        Check(InPass && count == 6 && instances == 1, "expected one fullscreen indexed draw");
        Events.emplace_back("draw");
    }
    void SetViewport(const RHIViewport &view) override { Check(view.Height > 0, "2D requires top-left framebuffer coordinates"); }
    void SetScissor(const RHIScissorRect &) override {}
    void PushConstants(const RHIPipeline &, RHIShaderStage, uint32_t, uint32_t size, const void *data) override {
        const auto *bytes = static_cast<const std::byte *>(data);
        Constants.emplace_back(bytes, bytes + size);
    }
    void RegisterCompletionCallback(std::function<void()>) override {}
    void TransitionTexture(RHITexture &, RHITextureState) override { Check(!InPass, "barrier inside pass"); }
    void CopyBufferToTexture(const RHIUploadBuffer &buffer, RHITexture &texture, uint64_t offset) override {
        Check(!InPass, "copy inside pass");
        const auto &bytes = dynamic_cast<const MockUpload &>(buffer).Bytes;
        const auto count = size_t(texture.Desc.Width) * texture.Desc.Height * texture.Desc.Layers * 4;
        Check(offset + count <= bytes.size(), "copy outside upload buffer");
        Copies.emplace_back(&texture, std::vector<std::byte>(bytes.begin() + offset, bytes.begin() + offset + count));
        Events.emplace_back("copy");
    }
    void CopyBufferToTextureRegion(const RHIUploadBuffer &buffer, RHITexture &texture, const RHITextureCopyRegion &region) override {
        Check(!InPass, "region copy inside pass");
        const auto &bytes = dynamic_cast<const MockUpload &>(buffer).Bytes;
        Check(TextureCopyRequiredBytes(texture.Desc, region) <= bytes.size(), "region copy outside upload buffer");
        const uint32_t stride = region.BufferRowLength ? region.BufferRowLength : region.Width;
        std::vector<std::byte> packed;
        for (uint32_t row = 0; row < region.Height; ++row) {
            const size_t first = region.BufferOffset + size_t(row) * stride * 4;
            packed.insert(packed.end(), bytes.begin() + first, bytes.begin() + first + size_t(region.Width) * 4);
        }
        Copies.emplace_back(&texture, std::move(packed));
        Regions.push_back(region);
        Events.emplace_back("copy");
    }
    void BeginColorPass(RHITexture &) override {
        Check(!InPass, "nested pass");
        InPass = true;
        Events.emplace_back("world");
    }
    void BeginPresentPass() override {
        Check(!InPass, "nested pass");
        InPass = true;
        Events.emplace_back("post");
    }
    void EndPass() override {
        Check(InPass, "unbalanced pass");
        InPass = false;
        Events.emplace_back("end");
    }
};
struct MockRHI : DynamicRHI {
    MockCommands Commands;
    RHIPresentationExtent Extent{101, 77};
    uint32_t Submits = 0, Waits = 0, InputReads = 0;
    bool FailRecord = false, SRGB = true;
    void RHIInit(PlatformInterface *) override {}
    void ThreadResourceInit() override {}
    void CreateSyncObjects() override {}
    RHIFrameBeginResult NewFrame() override { return {true, RHIFrameSkipReason::None, Commands.Token}; }
    void EndFrame(const RHIFrameToken &) override {}
    void Present(const RHIFrameToken &) override {}
    void AbortFrame() override {}
    void WaitIdle() override { ++Waits; }
    void Clear() override {}
    uint64_t GetSwapchainGeneration() const override { return 1; }
    RHIPresentationExtent GetPresentationExtent() const override { return Extent; }
    RHIBuffer *CreateVertexBuffer(uint32_t, uint32_t, const void *) override { return new RHIBuffer; }
    RHIBuffer *CreateIndexBuffer(uint32_t, uint32_t, const void *) override { return new RHIBuffer; }
    void DestroyBuffer(RHIBuffer *buffer) override { delete buffer; }
    void RecordForPresent(std::function<void(RHICommandList &)>) override { throw std::logic_error("2D used legacy present wrapper"); }
    uint32_t GetFrameSlotCount() const override { return 3; } // Deliberately not Vulkan's two slots.
    RHIFrameToken GetActiveFrameToken() const override { return Commands.Token; }
    bool IsPresentationSRGB() const override { return SRGB; }
    std::unique_ptr<RHITexture> CreateTexture(const RHITextureDesc &desc) override { return std::make_unique<MockImage>(desc); }
    std::unique_ptr<RHISampler> CreateNearestSampler() override { return std::make_unique<RHISampler>(); }
    std::unique_ptr<RHIUploadBuffer> CreateUploadBuffer(uint64_t size) override { return std::make_unique<MockUpload>(size); }
    std::unique_ptr<RHIPipeline> CreateRasterPipeline(const RHIRasterPipelineDesc &) override { return std::make_unique<RHIPipeline>(); }
    std::unique_ptr<RHIDescriptorPool> CreateImageDescriptorPool(uint32_t sets, uint32_t images, uint32_t samplers) override {
        Check(sets == 33 && images == 45 && samplers == 15, "descriptor pool is not sized from actual slots");
        return std::make_unique<RHIDescriptorPool>();
    }
    std::unique_ptr<RHIDescriptorSet> CreateImageDescriptorSet(RHIDescriptorPool &, const RHIPipeline &, std::span<const RHIImageBinding>) override {
        return std::make_unique<RHIDescriptorSet>();
    }
    void RecordFrame(const std::function<void(RHICommandList &)> &record) override {
        Commands.Events.clear();
        Commands.Copies.clear();
        Commands.Constants.clear();
        Commands.Regions.clear();
        record(Commands);
        Check(!Commands.InPass, "unfinished frame pass");
        if (FailRecord)
            throw std::runtime_error("injected recording failure");
        ++Submits;
    }
};
void ResizePresentation(MockRHI &rhi, RenderPolicyInterface &policy, RHIPresentationExtent extent) {
    const RHIPresentationChanged change{
        .Previous = {.Extent = rhi.Extent},
        .Current = {.Extent = extent}};
    rhi.Extent = extent;
    // Exercise virtual dispatch, as Render does when forwarding the RHI event.
    policy.OnPresentationChanged(change);
}
GridTextureCatalog Catalog() {
    GridTextureCatalog catalog{2, 2, 4, {}};
    catalog.Pixels.resize(64, std::byte{255});
    return catalog;
}
void InputValidation() {
    auto catalog = Catalog();
    ValidateGridTextureCatalog(catalog);
    catalog.Pixels.pop_back();
    Reject([&] { ValidateGridTextureCatalog(catalog); });
    catalog = {UINT32_MAX, UINT32_MAX, UINT32_MAX, {}};
    Reject([&] { ValidateGridTextureCatalog(catalog); });
    std::vector<uint32_t> cells(Grid2D::CellCount, 1);
    WorldGridFrameView frame{cells, {}, {}};
    ValidateWorldGridFrame(frame, 4, 101, 77);
    frame.Cells = std::span(cells).first(cells.size() - 1);
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.Cells = cells;
    cells.back() = 4;
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    cells.back() = 1;
    frame.DisplayMode = GridDisplayMode::ForceField;
    cells.back() = 0x8080FF;
    ValidateWorldGridFrame(frame, 4, 101, 77);
    cells.back() = 0x1000000;
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    cells.back() = 1;
    frame.DisplayMode = GridDisplayMode(100);
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.DisplayMode = GridDisplayMode::Materials;
    frame.View.CellSizePixels[0] = 0;
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.View = {};
    frame.View.CellSizePixels[1] = UINT32_MAX;
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.View = {};
    frame.View.OriginPixels[0] = INT32_MAX;
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.View = {};
    frame.View.OriginPixels = {-16, -8};
    ValidateWorldGridFrame(frame, 4, 101, 77);
    frame.View.Background[3] = 0.5f;
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.View = {};
    frame.Wind.Mode = WindVisualMode(99);
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.Wind = {};
    frame.HasWind = true;
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.HasWind = false;
    frame.WindTimeSeconds = INFINITY;
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.WindTimeSeconds = 0;
    frame.Wind.Opacity = NAN;
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
    frame.Wind = {};
    frame.Post.Values[0] = std::numeric_limits<float>::quiet_NaN();
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
}

void UnpublishedFrameCapacity() {
    WorldGridFrame frame;
    const size_t cells = size_t(Grid2D::RegionSize) * Grid2D::RegionSize;
    Check(frame.GetWidth() == Grid2D::RegionSize && frame.GetHeight() == Grid2D::RegionSize && frame.Cells().size() == cells && frame.WindCells().size() == cells,
          "unpublished frame must start with one scheduling region");
    Check(frame.GetMemoryUsage().ReservedHeapBytes == cells * 2 * sizeof(uint32_t),
          "unpublished frame retained a full-world texture allocation");
}
void FrameRecording() {
    MockRHI rhi;
    std::vector<uint32_t> cells(Grid2D::CellCount, 1);
    PixelRender policy(rhi, Catalog(), [&](WorldGridFrame &output) {
        ++rhi.InputReads;
        output.SetRegion({0, 0}, Grid2D::Width, Grid2D::Height);
        std::copy(cells.begin(), cells.end(), output.Cells().begin());
    });
    Reject([&] { policy.FrameBegin(); });
    policy.Initialize();
    Check(rhi.InputReads == 0, "initialization read gameplay before GameBegin");
    Reject([&] { policy.Draw(); });
    std::array<const RHITexture *, 3> slotImages{};
    for (uint32_t frame = 0; frame < 6; ++frame) {
        rhi.Commands.Token = {frame + 1, 1, frame % 3, 0};
        for (size_t i = 0; i < cells.size(); ++i)
            cells[i] = uint32_t((i / Grid2D::Width + frame) % 3) + 1;
        policy.FrameBegin();
        Check(rhi.InputReads == frame + 1, "FrameBegin must publish exactly once");
        const auto last = cells.back();
        cells.back() = 0; // Gameplay edits after publication must wait until next frame.
        policy.Draw();
        Check(rhi.InputReads == frame + 1, "Draw read mutable gameplay");
        cells.back() = last;
        const auto &copies = rhi.Commands.Copies;
        Check(copies.size() == (frame == 0 ? 2 : 1), "expected one dynamic world upload and one initial material upload");
        const size_t start = copies.size() - 1;
        Check(copies[start].second.size() == Grid2D::UploadBytes, "incorrect dynamic texture size");
        Check(std::memcmp(copies[start].second.data(), cells.data(), Grid2D::UploadBytes) == 0,
              "uploaded stale or incorrectly indexed world data");
        if (frame < 3)
            slotImages[frame] = copies[start].first;
        else
            Check(copies[start].first == slotImages[frame % 3], "resources not reused by frame slot");
        const auto &events = rhi.Commands.Events;
        const std::vector<std::string> suffix{"world", "draw", "end", "post", "draw", "end"};
        Check(std::equal(suffix.begin(), suffix.end(), events.end() - 6), "expected exactly two ordered passes");
        Check(events.size() == copies.size() + 6, "unexpected extra pass/draw");
    }
    Check(slotImages[0] != slotImages[1] && slotImages[1] != slotImages[2], "shared writable images between in-flight slots");
    Check(policy.UploadedWorldBytes() == 6 * Grid2D::UploadBytes && rhi.Waits == 0, "unexpected per-frame idle or missing upload");
    ResizePresentation(rhi, policy, {81, 51});
    Check(rhi.Waits == 1, "resize did not wait for GPU work before rebuilding colors");
    for (uint32_t slot = 0; slot < rhi.GetFrameSlotCount(); ++slot)
        Check(policy.SceneColor(slot).Desc.Width == 81 && policy.SceneColor(slot).Desc.Height == 51,
              "resize did not rebuild colors for every frame slot");
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.Commands.Copies.size() == 1, "resize reuploaded immutable materials");
    policy.Clear();
    policy.Clear();
    Check(rhi.InputReads == 7, "cleanup invoked a dead gameplay callback");
    rhi.SRGB = false;
    PixelRender unsupported(rhi, Catalog(), [](WorldGridFrame &) {});
    Reject([&] { unsupported.Initialize(); });
}
void CharacterComposition() {
    MockRHI rhi;
    CharacterDraw character{{12.25f, -2.5f, 32, 64}, {1, 0.5f, 0.25f, 1}, {0, 1, 0.5f, 1}};
    bool show = true;
    PixelRender policy(rhi, Catalog(), [&](WorldGridFrame &output) {
        output.Character = show ? std::optional{character} : std::nullopt;
    });
    policy.Initialize();
    policy.FrameBegin();
    character.Bounds[0] = 100;
    policy.Draw();
    const auto &events = rhi.Commands.Events;
    const std::vector<std::string> suffix{"world", "draw", "end", "world", "draw", "end", "post", "draw", "end"};
    Check(std::equal(suffix.begin(), suffix.end(), events.end() - suffix.size()), "character pass must follow world and precede post");
    CharacterDraw uploaded;
    Check(rhi.Commands.Constants.size() == 3 && rhi.Commands.Constants[1].size() == sizeof(uploaded), "character constants layout");
    std::memcpy(&uploaded, rhi.Commands.Constants[1].data(), sizeof(uploaded));
    Check(uploaded.Bounds[0] == 12.25f, "Draw used live character or rounded its position");
    Check(&policy.SceneColor(0) != &policy.WorldColor(0), "character composite must have separate target");
    ResizePresentation(rhi, policy, {82, 52});
    policy.FrameBegin();
    policy.Draw();
    Check(policy.SceneColor(0).Desc.Width == 82 && policy.SceneColor(0).Desc.Height == 52, "character target not resized");
    show = false;
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.Commands.Constants.size() == 2 && &policy.SceneColor(0) == &policy.WorldColor(0), "absent character must skip composition");
    character.Bounds[0] = NAN;
    show = true;
    Reject([&] { policy.FrameBegin(); });
    policy.Clear();
}
void WindComposition() {
    MockRHI rhi;
    WindVisualMode mode = WindVisualMode::ShortTrails;
    GridDisplayMode display = GridDisplayMode::Materials;
    bool character = true;
    uint32_t velocity = PackWindVelocity(-25, 40);
    PixelRender policy(rhi, Catalog(), [&](WorldGridFrame &frame) {
        frame.SetRegion({0, 0}, Grid2D::Width, Grid2D::Height);
        frame.DisplayMode = display;
        frame.Wind.Mode = mode;
        frame.WindTimeSeconds = .125f;
        frame.HasWind = true;
        std::fill(frame.WindCells().begin(), frame.WindCells().end(), velocity);
        frame.Character = character ? std::optional{CharacterDraw{{0, 0, 4, 8}, {1, 1, 1, 1}, {0, 1, 0, 1}}} : std::nullopt;
    });
    policy.Initialize();
    std::array<const RHITexture *, 3> windImages{};
    for (uint32_t slot = 0; slot < 3; ++slot) {
        rhi.Commands.Token.Slot = slot;
        policy.FrameBegin();
        velocity = PackWindVelocity(300, -30);
        policy.Draw();
        const auto &copies = rhi.Commands.Copies;
        Check(copies.size() == (slot == 0 ? 3 : 2), "wind texture not uploaded exactly once");
        Check(copies.back().second.size() == Grid2D::UploadBytes, "incorrect wind upload size");
        uint32_t packed;
        std::memcpy(&packed, copies.back().second.data(), sizeof(packed));
        Check(packed == PackWindVelocity(-25, 40), "Draw read live wind data");
        windImages[slot] = copies.back().first;
        const auto &constants = rhi.Commands.Constants;
        Check(constants.size() == 4 && constants[1].size() == 64 && constants[2].size() == 64, "wind must precede character and post passes");
        uint32_t shaderMode;
        std::memcpy(&shaderMode, constants[1].data() + 24, sizeof(shaderMode));
        Check(shaderMode == 1, "wind mode constant layout mismatch");
        velocity = PackWindVelocity(-25, 40);
    }
    Check(windImages[0] != windImages[1] && windImages[1] != windImages[2], "wind uploads share in-flight textures");
    mode = WindVisualMode::AirRefraction;
    character = false;
    policy.FrameBegin(); policy.Draw();
    Check(rhi.Commands.Constants.size() == 3 && &policy.SceneColor(2) != &policy.WorldColor(2), "wind-only scene not selected");
    ResizePresentation(rhi, policy, {65, 49});
    policy.FrameBegin(); policy.Draw();
    Check(policy.SceneColor(2).Desc.Width == 65 && policy.SceneColor(2).Desc.Height == 49, "wind target resize failed");
    mode = WindVisualMode::Off;
    policy.FrameBegin(); policy.Draw();
    Check(rhi.Commands.Copies.size() == 1 && rhi.Commands.Constants.size() == 2 && &policy.SceneColor(2) == &policy.WorldColor(2), "Off retains stale wind output");
    mode = WindVisualMode::AirRefraction;
    display = GridDisplayMode::ForceField;
    policy.FrameBegin(); policy.Draw();
    Check(rhi.Commands.Copies.size() == 1 && rhi.Commands.Constants.size() == 2, "wind affects F1 diagnostic pass");
    policy.Clear();
}
void FailedFrames() {
    for (int failure = 0; failure < 3; ++failure) {
        MockRHI rhi;
        PixelRender policy(rhi, Catalog(), [failure](WorldGridFrame &output) {
            std::fill(output.Cells().begin(), output.Cells().end(), failure == 0 ? 4 : 1);
            if (failure == 1)
                throw std::runtime_error("injected publication failure");
        });
        policy.Initialize();
        if (failure < 2) {
            Reject([&] { policy.FrameBegin(); });
            Reject([&] { policy.Draw(); });
        } else {
            policy.FrameBegin();
            rhi.FailRecord = true;
            Reject([&] { policy.Draw(); });
        }
        Check(rhi.Submits == 0 && policy.SubmittedFrames() == 0, "failed frame submitted or counted");
        if (failure == 2) {
            Check(policy.UploadedWorldBytes() == 0, "failed recording committed upload accounting");
            rhi.FailRecord = false;
            policy.FrameBegin();
            policy.Draw();
            Check(rhi.Submits == 1 && policy.SubmittedFrames() == 1, "recording failure prevented a fresh frame");
            Check(rhi.Commands.Copies.size() == 2, "retry skipped uncommitted material or world upload");
        }
        policy.Clear();
    }
}
void GameplayPublication() {
    MockRHI rhi;
    auto world = std::make_unique<pixel::World>(Grid2D::Width, Grid2D::Height);
    auto snapshot = std::make_shared<pixel::WorldSnapshot>(Grid2D::Width, Grid2D::Height);
    const std::weak_ptr<pixel::WorldSnapshot> lifetime = snapshot;
    CellAppearanceTable appearances{{10, 2}, {20, 1}};
    GridView2D view;
    PostProcessParameters post;
    PixelRender policy(rhi, Catalog(), [&, snapshot](WorldGridFrame &output) {
        ++rhi.InputReads;
        world->PublishState(*snapshot);
        BuildWorldGridFrame(snapshot->Read(), appearances, output);
        output.View = view;
        output.Post = post;
    });
    snapshot.reset(); // Render is now the sole owner of the published gameplay state.
    policy.Initialize();
    world->SetCell(0, 0, {pixel::CellTypeId::Dirt});
    policy.FrameBegin();
    world->SetCell(1, 0, {static_cast<pixel::CellTypeId>(20)});
    view.OriginPixels[0] = 8;
    post.Values[0] = 0.5f;
    policy.Draw();
    auto frame = policy.PublishedFrame();
    Check(frame.Cells[0] == 2 && frame.Cells[1] == 0 && frame.View.OriginPixels[0] == 0 && frame.Post.Values[0] == 1, "frame mixed pre/post-tick state");
    policy.FrameBegin();
    policy.Draw();
    frame = policy.PublishedFrame();
    Check(frame.Cells[0] == 2 && frame.Cells[1] == 1 && frame.View.OriginPixels[0] == 8 && frame.Post.Values[0] == 0.5f, "next frame lost persistent or newly published state");
    world.reset(); // Mirrors Engine shutdown: Game dies before Render.Clear.
    policy.Clear();
    Check(lifetime.expired() && rhi.InputReads == 2, "Clear retained snapshot or invoked dead gameplay");
}
void DebugFrameIsolation() {
    MockRHI rhi;
    pixel::World world(Grid2D::Width, Grid2D::Height);
    auto mode = GridDisplayMode::ForceField;
    PixelRender policy(rhi, Catalog(), [&](WorldGridFrame &output) {
        ++rhi.InputReads;
        output.SetRegion({0, 0}, Grid2D::Width, Grid2D::Height);
        if (mode == GridDisplayMode::ForceField)
            BuildForceFieldFrame(world, output);
        else
            BuildWorldGridFrame(world.ReadState(), {}, output);
    });
    policy.Initialize();
    policy.FrameBegin();
    mode = GridDisplayMode::Materials;
    world.FillForceDirection({0, -1});
    policy.Draw();
    Check(rhi.InputReads == 1 && policy.PublishedFrame().DisplayMode == GridDisplayMode::ForceField, "Draw read an unpublished debug mode");
    Check(policy.PublishedFrame().Cells.front() == 0x80FF80, "Draw read an unpublished force direction");
    uint32_t uploaded = 0, shaderMode = 0;
    std::memcpy(&uploaded, rhi.Commands.Copies[1].second.data(), sizeof(uploaded));
    Check(rhi.Commands.Constants.size() == 2 && rhi.Commands.Constants[0].size() == 96, "unexpected world/post constant layout");
    std::memcpy(&shaderMode, rhi.Commands.Constants[0].data() + 48, sizeof(shaderMode));
    Check(uploaded == 0x80FF80 && shaderMode == 1, "uploaded debug data and shader mode are inconsistent");
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.InputReads == 2 && policy.PublishedFrame().DisplayMode == GridDisplayMode::Materials && policy.PublishedFrame().Cells.front() == 0, "next frame did not restore material IDs");
    std::memcpy(&shaderMode, rhi.Commands.Constants[0].data() + 48, sizeof(shaderMode));
    Check(shaderMode == 0, "material frame retained debug shader mode");
    policy.Clear();
}

void RegionUploadCaching() {
    MockRHI rhi;
    pixel::World world(130, 70);
    pixel::WorldSnapshot snapshot;
    CellAppearanceTable appearances{{10, 1}};
    bool forceView = false;
    PixelRender policy(rhi, Catalog(), [&](WorldGridFrame &output) {
        world.PublishState(snapshot);
        if (forceView)
            BuildForceFieldFrame(world, output);
        else
            BuildWorldGridFrame(snapshot.Read(), appearances, output);
    });
    policy.Initialize();
    for (uint32_t slot = 0; slot < 3; ++slot) {
        rhi.Commands.Token.Slot = slot;
        policy.FrameBegin();
        policy.Draw();
        Check(rhi.Commands.Copies.back().first->Desc.Width == 130 && rhi.Commands.Copies.back().first->Desc.Height == 70,
              "dynamic grid dimensions retained the old world pages");
    }
    const auto initialBytes = policy.UploadedWorldBytes();
    const auto initialWrites = MockUpload::Writes;
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.Commands.Copies.empty() && MockUpload::Writes == initialWrites && policy.UploadedWorldBytes() == initialBytes,
          "unchanged material regions were mapped or uploaded again");

    world.PlaceDirt(65, 65);
    policy.FrameBegin();
    world.RemoveDirt(65, 65);
    policy.Draw();
    Check(rhi.Commands.Regions.size() == 1 && rhi.Commands.Regions.front().X == 64 && rhi.Commands.Regions.front().Y == 64 && rhi.Commands.Regions.front().Width == 64 && rhi.Commands.Regions.front().Height == 6,
          "a cell edit did not upload only its clipped 64-cell region");
    Check(policy.PublishedFrame().Cells[65 * 130 + 65] == 1,
          "editing after publication changed the pending region upload");
    policy.FrameBegin();
    policy.Draw();
    Check(policy.PublishedFrame().Cells[65 * 130 + 65] == 0, "following publication lost a removal");

    world.SetForceDirection(10, 10, {0, -1});
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.Commands.Copies.empty(), "base force metadata caused an unchanged material texture upload");
    world.PlaceDirt(1, 1);
    policy.FrameBegin();
    policy.Draw();
    appearances[10] = 2;
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.Commands.Regions.size() == 1 && policy.PublishedFrame().Cells[131] == 2,
          "appearance mapping change failed to invalidate the affected cached region");
    forceView = true;
    policy.FrameBegin();
    policy.Draw();
    Check(!rhi.Commands.Copies.empty() && policy.PublishedFrame().DisplayMode == GridDisplayMode::ForceField,
          "F1 retained material texture data");
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.Commands.Copies.empty(), "unchanged force-field colours were uploaded again");
    ResizePresentation(rhi, policy, {111, 79});
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.Commands.Regions.size() == 1 && rhi.Commands.Regions.front().Width == 130,
          "resource reconstruction did not synchronize the active frame slot");
    policy.Clear();
}

void TerrainTexturePhases() {
    MockRHI rhi;
    auto textures = Catalog();
    textures.CellsPerTile = 64;
    std::array<int64_t, 2> origin{(int64_t{1} << 55) + 19, -(int64_t{1} << 55) - 7};
    PixelRender policy(rhi, textures, [&](WorldGridFrame &output) {
        output.SetRegion(origin, 64, 64);
        output.View.WorldOriginCells = origin;
        std::fill(output.Cells().begin(), output.Cells().end(), 0);
        output.Cells().front() = Grid2D::SurfaceCellFlag | (3u << Grid2D::SurfaceStripShift) | 1u;
    });
    policy.Initialize();
    const auto check = [&] {
        policy.FrameBegin();
        policy.Draw();
        std::array<uint32_t, 3> tiling;
        Check(rhi.Commands.Constants[0].size() == 96, "terrain tiling changed the shader layout");
        std::memcpy(tiling.data(), rhi.Commands.Constants[0].data() + 52, sizeof(tiling));
        const auto phase = [](int64_t cell) { return uint32_t((cell % 64 + 64) % 64); };
        Check(tiling == std::array<uint32_t, 3>{64, phase(origin[0]), phase(origin[1])},
              "negative or distant terrain UV phase lost integer precision");
    };
    check();
    origin[0] += 13;
    origin[1] -= 29;
    check();
    policy.Clear();
}

void DistantRegionProjection() {
    WorldGridFrame output;
    constexpr int64_t distant = int64_t{1} << 60;
    output.SetRegion({distant - 64, -distant}, 128, 64);
    output.View.WorldOriginCells = {distant, -distant};
    output.View.OriginPixels = {3, -2};
    const auto local = LocalGridPixelOrigin(output.Read());
    Check(local == std::array<int32_t, 2>{1027, -2}, "distant region lost integer cell precision");
    const auto picked = PickGridCell(output.View, {0, 0}, {80, 60}, {80, 60});
    Check(picked && *picked == std::array<int64_t, 2>{distant, -distant - 1},
          "negative or distant coordinate picking truncated the cell coordinate");
    const auto region = VisibleWorldRegion(output.View, {80, 60});
    Check(region.X % Grid2D::RegionSize == 0 && region.Y % Grid2D::RegionSize == 0 && region.Width >= 64 && region.Height >= 64,
          "visible region omitted the aligned wind backtrace halo");
    output.View.WorldOriginCells[0] = INT64_MIN;
    Reject([&] { LocalGridPixelOrigin(output.Read()); });

    pixel::Camera2D camera;
    const pixel::CellCoord chunk{int64_t{1} << 45, -(int64_t{1} << 45)};
    camera.SetWorldPosition({chunk, {512.125, 510.0625}});
    const auto view = MakeCameraView(camera, {1600, 1200}, {});
    const auto normalized = camera.GetWorldPosition();
    const std::array<int32_t, 2> expectedOrigin{int32_t(std::round(normalized.Local[0] * 16 - 800)),
                                                int32_t(std::round(normalized.Local[1] * 16 - 600))};
    Check(view.OriginPixels == expectedOrigin && view.WorldOriginCells == std::array<int64_t, 2>{normalized.Chunk.X * pixel::storage::ChunkEdge, normalized.Chunk.Y * pixel::storage::ChunkEdge},
          "distant camera discarded its fractional position before GPU projection");
    pixel::Body player;
    player.OriginChunk = chunk;
    player.X = 511.03125;
    player.Y = 508;
    output.View = view;
    const pixel::ActorSnapshot snapshot{pixel::ActorId{1}, pixel::ActorKind::Player, player.GetWorldPosition(), player.Grounded};
    BuildCharacterFrame(snapshot, camera, {1600, 1200}, output);
    Check(output.Character && output.Character->Bounds == std::array<float, 4>{782.5f, 567.f, 32, 64},
          "distant character geometry lost its fractional camera-relative offset");
}

void UnalignedRegionPublication() {
    pixel::World world(128, 128);
    pixel::WorldSnapshot snapshot;
    WorldGridFrame frame;
    const pixel::WorldRect region{1, 1, 64, 64};
    world.PublishSnapshot(region, snapshot);
    BuildWorldGridFrame(snapshot.Read(), {{10, 1}}, frame);
    Check(frame.Cells().back() == 0, "unaligned snapshot did not publish its initial empty edge");
    world.PlaceDirt(64, 64);
    world.PublishSnapshot(region, snapshot);
    BuildWorldGridFrame(snapshot.Read(), {{10, 1}}, frame);
    Check(frame.Cells().back() == 1, "render tile ignored a change in an overlapping quadtree leaf");
    world.RemoveDirt(64, 64);
    Check(frame.Cells().back() == 1, "unaligned snapshot retained mutable world storage");
}
void BackgroundCaching() {
    MockRHI rhi;
    auto image = std::make_shared<pixel::ArtImage>();
    image->Width = image->Height = 1;
    image->Pixels.assign(4, std::byte{255});
    auto assets = std::make_shared<pixel::BackgroundAssets>();
    assets->Layers.push_back({image, 4096});
    bool field = false;
    PixelRender policy(rhi, Catalog(), [&](WorldGridFrame &frame) {
        frame.SetRegion({0, 0}, 64, 64);
        std::fill(frame.Cells().begin(), frame.Cells().end(), 0);
        frame.DisplayMode = field ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
    }, "Pixel2DPostProcess.fs", assets);
    policy.Initialize();
    for (uint32_t index = 0; index < 6; ++index) {
        rhi.Commands.Token.Slot = index % 3;
        policy.FrameBegin(); policy.Draw();
        Check(rhi.Commands.Copies.size() == (index == 0 ? 3 : 1), "immutable art was not uploaded exactly once across frame slots");
        float count = 0;
        std::memcpy(&count, rhi.Commands.Constants[0].data() + 92, sizeof(count));
        Check(count == 1, "art layer count was not sent to shader");
    }
    ResizePresentation(rhi, policy, {201, 121});
    policy.FrameBegin(); policy.Draw();
    Check(rhi.Commands.Copies.size() == 1, "resize uploaded cached background again");
    field = true;
    policy.FrameBegin(); policy.Draw();
    float count = 1;
    std::memcpy(&count, rhi.Commands.Constants[0].data() + 92, sizeof(count));
    Check(count == 0, "art was enabled in diagnostic view");
    policy.Clear();
    Check(assets->Layers.front().Image == image, "render cleanup invalidated cached CPU art");
}
} // namespace
// This test checks policy orchestration without shader/device dependencies. Real
// shader reflection and pixel results are checked by the opt-in Vulkan smoke.
const SPIRVInfo &GetShaderInfo(std::string_view) {
    static SPIRVInfo info;
    return info;
}
int main(int argc, char **argv) {
    try {
        const bool renderingOnly = argc == 2 && std::string_view(argv[1]) == "--rendering-only";
        InputValidation();
        UnpublishedFrameCapacity();
        if (!renderingOnly) {
            RunPixelWorldTests();
            RunPixelFieldTests();
            RunPixelWindTests();
            RunPixelDirtWindTests();
        }
        RunPixelFieldViewTests();
        RunPixelWindVisualTests();
        WindComposition();
        FrameRecording();
        CharacterComposition();
        FailedFrames();
        GameplayPublication();
        DebugFrameIsolation();
        RegionUploadCaching();
        DistantRegionProjection();
        TerrainTexturePhases();
        UnalignedRegionPublication();
        BackgroundCaching();
        std::cout << "Pixel2D input and frame orchestration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
