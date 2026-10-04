#include "HAL/Shader/ShaderLibrary.h"
#include "PixelRender/PixelRender.h"
#include "PixelRender/PixelWorldPresentation.h"
#include "World/World.h"
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
    std::vector<std::byte> Bytes;
    explicit MockUpload(size_t size) : Bytes(size) {}
    void Write(std::span<const std::byte> bytes) override {
        Check(bytes.size() == Bytes.size(), "partial upload");
        std::copy(bytes.begin(), bytes.end(), Bytes.begin());
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
        Check(sets == 6 && images == 18 && samplers == 6, "descriptor pool is not sized from actual slots");
        return std::make_unique<RHIDescriptorPool>();
    }
    std::unique_ptr<RHIDescriptorSet> CreateImageDescriptorSet(RHIDescriptorPool &, const RHIPipeline &, std::span<const RHIImageBinding>) override {
        return std::make_unique<RHIDescriptorSet>();
    }
    void RecordFrame(std::function<void(RHICommandList &)> record) override {
        Commands.Events.clear();
        Commands.Copies.clear();
        Commands.Constants.clear();
        record(Commands);
        Check(!Commands.InPass, "unfinished frame pass");
        if (FailRecord)
            throw std::runtime_error("injected recording failure");
        ++Submits;
    }
};
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
    frame.Post.Values[0] = std::numeric_limits<float>::quiet_NaN();
    Reject([&] { ValidateWorldGridFrame(frame, 4, 101, 77); });
}
void FrameRecording() {
    MockRHI rhi;
    std::vector<uint32_t> cells(Grid2D::CellCount, 1);
    PixelRender policy(rhi, Catalog(), [&](WorldGridFrame &output) {
        ++rhi.InputReads;
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
        Check(copies.size() == (frame == 0 ? 5 : 4), "expected four world uploads every frame and one initial material upload");
        const size_t start = copies.size() - 4;
        for (size_t page = 0; page < 4; ++page) {
            Check(copies[start + page].second.size() == Grid2D::PageBytes, "incorrect data page size");
            Check(std::memcmp(copies[start + page].second.data(), reinterpret_cast<const std::byte *>(cells.data()) + page * Grid2D::PageBytes, Grid2D::PageBytes) == 0, "uploaded stale or incorrectly paged world data");
        }
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
    rhi.Extent = {81, 51};
    policy.OnPresentationChanged();
    Check(rhi.Waits == 1 && policy.SceneColor(2).Desc.Width == 81, "resize did not safely rebuild colors");
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.Commands.Copies.size() == 4, "resize reuploaded immutable materials");
    policy.Clear();
    policy.Clear();
    Check(rhi.InputReads == 7, "cleanup invoked a dead gameplay callback");
    rhi.SRGB = false;
    PixelRender unsupported(rhi, Catalog(), [](WorldGridFrame &) {});
    Reject([&] { unsupported.Initialize(); });
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
        policy.Clear();
    }
}
void GameplayPublication() {
    MockRHI rhi;
    auto world = std::make_unique<pixel::World>(Grid2D::Width, Grid2D::Height);
    auto snapshot = std::make_shared<pixel::WorldState>(Grid2D::Width, Grid2D::Height);
    const std::weak_ptr<pixel::WorldState> lifetime = snapshot;
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
    world->SetCell(0, 0, {{10}});
    policy.FrameBegin();
    world->SetCell(1, 0, {{20}});
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
    Check(rhi.Commands.Constants.size() == 2 && rhi.Commands.Constants[0].size() == 64, "unexpected world/post constant layout");
    std::memcpy(&shaderMode, rhi.Commands.Constants[0].data() + 48, sizeof(shaderMode));
    Check(uploaded == 0x80FF80 && shaderMode == 1, "uploaded debug data and shader mode are inconsistent");
    policy.FrameBegin();
    policy.Draw();
    Check(rhi.InputReads == 2 && policy.PublishedFrame().DisplayMode == GridDisplayMode::Materials && policy.PublishedFrame().Cells.front() == 0, "next frame did not restore material IDs");
    std::memcpy(&shaderMode, rhi.Commands.Constants[0].data() + 48, sizeof(shaderMode));
    Check(shaderMode == 0, "material frame retained debug shader mode");
    policy.Clear();
}
} // namespace
// This test checks policy orchestration without shader/device dependencies. Real
// shader reflection and pixel results are checked by the opt-in Vulkan smoke.
const SPIRVInfo &GetShaderInfo(std::string_view) {
    static SPIRVInfo info;
    return info;
}
int main() {
    try {
        InputValidation();
        RunPixelWorldTests();
        RunPixelFieldTests();
        RunPixelWindTests();
        RunPixelFieldViewTests();
        RunPixelDirtWindTests();
        FrameRecording();
        FailedFrames();
        GameplayPublication();
        DebugFrameIsolation();
        std::cout << "Pixel2D input and frame orchestration passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
