#include "HAL/Shader/ShaderLibrary.h"
#include "PixelRender/PixelRender.h"
#include "PixelRender/FormulaFrame.h"
#include "PixelRender/GamePresentation.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
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
    std::vector<const RHITexture *> Targets;
    std::vector<RHIViewport> Viewports;
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
    void SetViewport(const RHIViewport &view) override { Check(view.Height > 0, "2D requires top-left framebuffer coordinates"); Viewports.push_back(view); }
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
    void BeginColorPass(RHITexture &target) override {
        Targets.push_back(&target);
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
        Commands.Targets.clear();
        Commands.Viewports.clear();
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

void CircleProjection() {
    pixel::Camera2D camera;
    const pixel::CellCoord chunk{int64_t{1} << 45, -(int64_t{1} << 45)};
    camera.SetWorldPosition({chunk, {8.125, 9.25}});
    std::vector<pixel::FormulaProjectile> live(2);
    live[0].WorldEpoch = 4; live[0].CastId = 7; live[0].ProjectileId = 9;
    live[0].Position = {chunk, {10.25, 9.5}};
    live[1].Position = {chunk, {1000, 1000}};
    WorldGridFrame first, second;
    pixel::BuildFormulaFrame(live, camera, {160, 120}, first);
    pixel::BuildFormulaFrame(live, camera, {160, 120}, second);
    Check(first.FormulaDraws.size() == 1 && second.FormulaDraws.size() == 1, "offscreen ball was published");
    const auto circle = first.Read().FormulaDraws.front();
    Check(circle.CenterRadius == std::array<float, 4>{114, 64, 5.6f, 5.6f}, "fractional far-coordinate projection lost precision");
    Check(circle.WorldEpoch == 4 && circle.CastId == 7 && circle.ProjectileId == 9, "combined identity was lost");
    live[0].Position.Local[0] += 1;
    pixel::BuildFormulaFrame(live, camera, {160, 120}, second);
    Check(first.Read().FormulaDraws.front().CenterRadius[0] == 114 && second.Read().FormulaDraws.front().CenterRadius[0] == 130,
          "independent render frames retained shared gameplay storage");
    Check(first.GetFormulaMemoryUsage().ReservedHeapBytes >= sizeof(FormulaDraw), "circle memory is not reported separately");
    pixel::BuildFormulaFrame({}, camera, {160, 120}, first);
    Check(first.Read().FormulaDraws.empty(), "empty publication retained a ball");
}

void FormulaPassLifecycle() {
    MockRHI rhi;
    bool visible = true, wind = true, character = true, field = false;
    std::vector<FormulaDraw> live(2);
    live[0].CenterRadius = {4.25f, 5.5f, 3.f, 3.f};
    live[1].CenterRadius = {7.5f, 6.f, 3.f, 3.f}; // Overlapping circles remain in one colour pass.
    PixelRender policy(rhi, Catalog(), [&](WorldGridFrame &frame) {
        frame.SetRegion({0, 0}, 64, 64);
        std::fill(frame.Cells().begin(), frame.Cells().end(), 0);
        std::fill(frame.WindCells().begin(), frame.WindCells().end(), 0);
        frame.FormulaDraws = visible ? live : std::vector<FormulaDraw>{};
        frame.HasWind = wind;
        frame.Wind.Mode = WindVisualMode::ShortTrails;
        frame.DisplayMode = field ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
        frame.Character = character ? std::optional<CharacterDraw>(CharacterDraw{{1, 1, 8, 16}, {1, 1, 1, 1}, {1, 1, 1, 1}}) : std::nullopt;
    });
    policy.Initialize();
    Check(policy.FormulaTargetBytes() == uint64_t(101) * 77 * 4 * 3, "formula GPU allocation was not reported by frame slot");
    std::array<const RHITexture *, 3> circleTargets{};
    for (uint32_t slot = 0; slot < 3; ++slot) {
        rhi.Commands.Token.Slot = slot;
        policy.FrameBegin();
        live[0].CenterRadius[0] += 1; // Published vectors must survive a live mutation.
        policy.Draw();
        Check(rhi.Commands.Targets.size() == 4, "expected world, wind, formula and character colour passes");
        Check(rhi.Commands.Constants.size() == 7, "circle pass must copy scene once, then draw each ball once");
        Check(rhi.Commands.Constants[2].size() == 48 && rhi.Commands.Constants[3].size() == 48 && rhi.Commands.Constants[4].size() == 48,
              "formula constants layout changed");
        uint32_t copyMode = 9, drawMode = 9;
        std::memcpy(&copyMode, rhi.Commands.Constants[2].data() + 32, sizeof(copyMode));
        std::memcpy(&drawMode, rhi.Commands.Constants[3].data() + 32, sizeof(drawMode));
        Check(copyMode == 0 && drawMode == 1, "formula copy/circle shader modes are inconsistent");
        const auto &small = rhi.Commands.Viewports[3];
        Check(small.Width < rhi.Extent.Width && small.Height < rhi.Extent.Height, "each ball must use a small viewport");
        circleTargets[slot] = rhi.Commands.Targets[2];
        Check(&policy.SceneColor(slot) == rhi.Commands.Targets[3], "character was not composed after formula balls");
    }
    Check(circleTargets[0] != circleTargets[1] && circleTargets[1] != circleTargets[2], "formula target was shared across frame slots");
    character = wind = false;
    policy.FrameBegin(); policy.Draw();
    Check(rhi.Commands.Targets.size() == 2 && &policy.SceneColor(2) == rhi.Commands.Targets[1], "world-only formula composition selected the wrong post input");
    visible = false;
    policy.FrameBegin(); policy.Draw();
    Check(rhi.Commands.Targets.size() == 1 && &policy.SceneColor(2) == &policy.WorldColor(2), "no-ball frame retained the formula colour target");
    visible = true; field = true;
    policy.FrameBegin(); policy.Draw();
    Check(rhi.Commands.Targets.size() == 1, "F1 displayed formula balls");
    field = false;
    ResizePresentation(rhi, policy, {201, 121});
    policy.FrameBegin(); policy.Draw();
    Check(policy.SceneColor(2).Desc.Width == 201 && policy.SceneColor(2).Desc.Height == 121, "formula target was not rebuilt on resize");
    policy.Clear();
}

void PublisherIsolation() {
    pixel::GameSession session;
    pixel::GameCamera camera;
    camera.SetFramebufferSize({160, 120});
    camera.GetCamera().SetPosition({20, 20});
    auto &runtime = session.GetFormulaRuntime();
    Check(runtime.Cast(session.GetWorld(), pixel::WorldPosition::FromAbsolute(20, 20), 0), "publisher fixture could not activate bolt");
    Check(runtime.Projectiles().size() == 1, "publisher fixture bolt was unexpectedly blocked");
    pixel::GamePresentation presentation;
    auto publishFirst = presentation.CreateFramePublisher(session, camera);
    auto publishSecond = presentation.CreateFramePublisher(session, camera);
    WorldGridFrame first, second;
    publishFirst(first);
    publishSecond(second);
    Check(first.Read().FormulaDraws.size() == 1 && second.Read().FormulaDraws.size() == 1, "publishers did not copy runtime balls");
    runtime.Clear();
    publishSecond(second);
    Check(first.Read().FormulaDraws.size() == 1 && second.Read().FormulaDraws.empty(), "runtime clear mutated an already published frame");
    presentation.DisplayMode = GridDisplayMode::ForceField;
    publishFirst(first);
    Check(first.Read().FormulaDraws.empty(), "F1 publication retained stale formula data");
}

void FormulaValidation() {
    WorldGridFrame frame;
    frame.FormulaDraws.push_back({{10, 10, 2, 2}});
    ValidateWorldGridFrame(frame.Read(), 4, 100, 100);
    frame.FormulaDraws.front().CenterRadius[2] = 0;
    Reject([&] { ValidateWorldGridFrame(frame.Read(), 4, 100, 100); });
    frame.FormulaDraws.front().CenterRadius[2] = 2;
    frame.FormulaDraws.front().Color[3] = 0.5f;
    Reject([&] { ValidateWorldGridFrame(frame.Read(), 4, 100, 100); });
}
} // namespace
const SPIRVInfo &GetShaderInfo(std::string_view) { static SPIRVInfo info; return info; }
int main() {
    try {
        CircleProjection();
        FormulaValidation();
        PublisherIsolation();
        FormulaPassLifecycle();
        std::cout << "Formula snapshot and render composition passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
