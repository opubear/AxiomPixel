#include "Axiom.h"
#include "Engine.h"
#include "HAL/Platform/PlatformInterface.h"
#include "HAL/RenderHardwareInterface/RHIFactory.h"
#include "HAL/Shader/ShaderLibrary.h"
#include "HAL/Vulkan/VulkanRasterResources.h"
#include "InputManager.h"
#include "PixelGame.h"
#include "PixelRender/PixelRender.h"
#include "SystemEnum.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
GridTextureCatalog Textures() {
    GridTextureCatalog catalog{2, 2, 4, {}};
    constexpr std::array<uint8_t, 64> pixels{
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255,
        255, 255, 0, 255, 0, 255, 255, 255, 255, 0, 255, 255, 0, 0, 0, 255,
        128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128, 128};
    catalog.Pixels.resize(pixels.size());
    std::memcpy(catalog.Pixels.data(), pixels.data(), pixels.size());
    return catalog;
}
std::vector<uint8_t> ReadColor(const RHITexture &texture) {
    const auto &image = dynamic_cast<const VulkanImageResource &>(texture);
    Require(image.State == RHITextureState::FragmentSampledRead, "readback source not in sampled state");
    const size_t size = size_t(image.Desc.Width) * image.Desc.Height * 4;
    auto buffer = std::make_shared<vkGPUBuffer>();
    vkBufferCreator::CreateBuffer(*buffer, size, vk::BufferUsageFlagBits::eTransferDst,
                                  vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
    VulkanCommandBuffer::RecordSingleTimeCommand([&](VulkanCommandBuffer &command) {
        vk::ImageMemoryBarrier barrier{};
        barrier.image = *image.Image;
        barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
        barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
        command.commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, barrier);
        vk::BufferImageCopy copy{};
        copy.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
        copy.imageExtent = vk::Extent3D{image.Desc.Width, image.Desc.Height, 1};
        command.commandBuffer.copyImageToBuffer(*image.Image, vk::ImageLayout::eTransferSrcOptimal, buffer->Buffer, copy);
        std::swap(barrier.oldLayout, barrier.newLayout);
        std::swap(barrier.srcAccessMask, barrier.dstAccessMask);
        command.commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, {}, {}, barrier);
        vk::MemoryBarrier host{vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead};
        command.commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, host, {}, {});
    },
                                                 buffer);
    std::vector<uint8_t> bytes(size);
    std::memcpy(bytes.data(), buffer->Map(size), size);
    buffer->Unmap();
    return bytes;
}
float Linear(uint8_t value) {
    const float v = float(value) / 255;
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
void VerifyColor(WorldGridFrameView frame, const RHITexture &texture, const GridTextureCatalog &catalog = Textures()) {
    const auto actual = ReadColor(texture);
    const auto extent = texture.Desc;
    // Dense deterministic samples include every texel phase and viewport edges.
    for (uint32_t y = 0; y < extent.Height; ++y) {
        for (uint32_t x = 0; x < extent.Width; ++x) {
            if (x > 32 && y > 32 && x + 1 != extent.Width && y + 1 != extent.Height && (x * 13 + y * 7) % 97 != 0)
                continue;
            const int64_t qx = int64_t(frame.View.OriginPixels[0]) + x;
            const int64_t qy = int64_t(frame.View.OriginPixels[1]) + y;
            auto expected = frame.View.Background;
            const auto cw = frame.View.CellSizePixels[0], ch = frame.View.CellSizePixels[1];
            if (qx >= 0 && qy >= 0 && qx < int64_t(Grid2D::Width) * cw && qy < int64_t(Grid2D::Height) * ch) {
                const auto id = frame.Cells[size_t(qy / ch) * Grid2D::Width + size_t(qx / cw)];
                if (frame.DisplayMode == GridDisplayMode::ForceField) {
                    for (size_t c = 0; c < 3; ++c)
                        expected[c] = float((id >> (8 * c)) & 255) / 255;
                } else if (id) {
                    const size_t tx = size_t((double(qx % cw) + .5) * catalog.Width / cw), ty = size_t((double(qy % ch) + .5) * catalog.Height / ch);
                    const size_t texel = ((id * catalog.Height + ty) * catalog.Width + tx) * 4;
                    const float alpha = float(std::to_integer<uint8_t>(catalog.Pixels[texel + 3])) / 255;
                    for (size_t c = 0; c < 3; ++c)
                        expected[c] = Linear(std::to_integer<uint8_t>(catalog.Pixels[texel + c])) * alpha + expected[c] * (1 - alpha);
                }
            }
            for (size_t c = 0; c < 4; ++c) {
                const int want = int(std::lround(expected[c] * 255));
                const int got = actual[(size_t(y) * extent.Width + x) * 4 + c];
                if (std::abs(want - got) > 2)
                    throw std::runtime_error("pixel mismatch x=" + std::to_string(x) + " y=" + std::to_string(y) + " channel=" + std::to_string(c) + " expected=" + std::to_string(want) + " actual=" + std::to_string(got));
            }
        }
    }
}
RHIFrameToken Draw(GameEngine &engine) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        engine.GetPlatform()->PollEvents();
        const auto frame = engine.FrameBegin();
        if (!frame.Ready) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        engine.GetInput()->Dispatch();
        engine.Tick(0, frame.Token);
        engine.FrameEnd(frame.Token);
        return frame.Token;
    }
    throw std::runtime_error("no drawable 2D frame");
}
void VerifyPostShader(GameEngine &engine, const RHITexture &source) {
    auto &rhi = *engine.GetRHI();
    const auto extent = rhi.GetPresentationExtent();
    auto target = rhi.CreateTexture({extent.Width, extent.Height, 1, RHITextureFormat::RGBA8UNorm, false, true});
    auto sampler = rhi.CreateNearestSampler();
    const std::array expected{DescriptorRequirement{0, 0, ShaderDescriptorType::SAMPLED_IMAGE, 1}, DescriptorRequirement{0, 1, ShaderDescriptorType::SAMPLER, 1}};
    auto pipeline = rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"), GetShaderInfo("Pixel2DPostProcess.fs"), expected, target.get(), 64});
    auto presentPipeline = rhi.CreateRasterPipeline({GetShaderInfo("Pixel2DFullscreen.vs"), GetShaderInfo("Pixel2DPostProcess.fs"), expected, nullptr, 64});
    auto pool = rhi.CreateImageDescriptorPool(2, 2, 2);
    const std::array bindings{RHIImageBinding{0, &source}, RHIImageBinding{1, nullptr, sampler.get()}};
    const std::array presentBindings{RHIImageBinding{0, target.get()}, RHIImageBinding{1, nullptr, sampler.get()}};
    auto set = rhi.CreateImageDescriptorSet(*pool, *pipeline, bindings);
    auto presentSet = rhi.CreateImageDescriptorSet(*pool, *presentPipeline, presentBindings);
    const std::array<float, 8> vertices{-1, -1, 1, -1, 1, 1, -1, 1};
    const std::array<uint32_t, 6> indices{0, 1, 2, 2, 3, 0};
    std::unique_ptr<RHIBuffer> vb(rhi.CreateVertexBuffer(8, sizeof(vertices), vertices.data()));
    std::unique_ptr<RHIBuffer> ib(rhi.CreateIndexBuffer(4, sizeof(indices), indices.data()));
    PostProcessParameters tint;
    tint.Values = {0.5f, 0.25f, 0.75f, 1};
    const auto begin = rhi.NewFrame();
    Require(begin.Ready, "post test frame unavailable");
    auto draw = [&](RHICommandList &cmd, const RHIPipeline &p, const RHIDescriptorSet &s, const PostProcessParameters &params) {
        cmd.SetViewport({0, 0, float(extent.Width), float(extent.Height), 0, 1});
        cmd.SetScissor({0, 0, extent.Width, extent.Height});
        cmd.BindVertexBuffer(*vb);
        cmd.BindIndexBuffer(*ib);
        cmd.BindPipeline(p);
        cmd.BindDescriptorSet(p, s, 0);
        cmd.PushConstants(p, RHIShaderStage::Fragment, 0, 64, &params);
        cmd.DrawIndexed(6);
        cmd.EndPass();
    };
    // Run the actual post shader against a readable attachment, then present it.
    // This is a shader verification frame, not a third pass added to the policy.
    try {
        rhi.RecordFrame([&](RHICommandList &cmd) {
            cmd.TransitionTexture(*target, RHITextureState::ColorAttachment);
            cmd.BeginColorPass(*target);
            draw(cmd, *pipeline, *set, tint);
            cmd.TransitionTexture(*target, RHITextureState::FragmentSampledRead);
            cmd.BeginPresentPass();
            draw(cmd, *presentPipeline, *presentSet, PostProcessParameters{});
        });
        rhi.Present(begin.Token);
        rhi.EndFrame(begin.Token);
        rhi.WaitIdle();
    } catch (...) {
        rhi.AbortFrame();
        throw;
    }
    const auto before = ReadColor(source), after = ReadColor(*target);
    for (size_t pixel = 0; pixel < before.size() / 4; ++pixel)
        for (size_t c = 0; c < 3; ++c)
            Require(std::abs(int(after[pixel * 4 + c]) - int(std::lround(before[pixel * 4 + c] * tint.Values[c]))) <= 1, "post-process tint or sampling incorrect");
}
void Lifecycle(int iteration) {
    Axiom app;
    app.Config = GameConfig::Load();
    auto game = std::make_unique<PixelGame>();
    auto *input = game.get();
    input->Appearances = {{10, 1}, {20, 2}, {30, 3}};
    PixelRender *policy = nullptr;
    uint64_t publications = 0;
    const auto *initialStorage = input->GetWorld().ReadState().Cells.data();
    // Independent fixture oracle: never derive expected pixels from the renderer's
    // converted output. Gameplay types intentionally differ from texture layers.
    std::vector<uint32_t> expectedCells(Grid2D::CellCount);
    for (uint32_t y = 0; y < Grid2D::Height; ++y)
        for (uint32_t x = 0; x < Grid2D::Width; ++x) {
            expectedCells[size_t(y) * Grid2D::Width + x] = 1 + (x / 4 + y / 4) % 3;
            input->GetWorld().SetCell(x, y, {{10 * expectedCells[size_t(y) * Grid2D::Width + x]}});
        }
    WorldGridFrameView expected{expectedCells, input->View, input->Post};
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.EnableUI = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi) {
        auto publish = input->CreateFramePublisher();
        auto result = std::make_unique<PixelRender>(rhi, Textures(), [&, publish = std::move(publish)](WorldGridFrame &frame) {
            ++publications;
            publish(frame);
        });
        policy = result.get();
        return result;
    };
    app.Engine = std::make_unique<GameEngine>(std::move(game), CreateRHI(), std::move(setup));
    auto &engine = *app.Engine;
    engine.EngineInit();
    engine.Begin();
    expected.View = input->View;
    Require(publications == 0, "world published before first Ready frame");
    auto first = Draw(engine);
    Require(publications == 1 && policy->PublishedFrame().Cells[0] == 1, "first frame did not publish initialized world");
    const auto *recycledStorage = input->GetWorld().ReadState().Cells.data();
    Require(initialStorage != recycledStorage, "first frame did not swap gameplay storage");
    engine.GetRHI()->WaitIdle();
    VerifyColor(expected, policy->SceneColor(first.Slot));
    Require(!engine.GetScene() && !engine.GetImgui(), "2D initialized 3D scene or UI");
    input->View.Background = {.1f, .2f, .3f, 1};
    const int32_t pagePixels = Grid2D::PageHeight * 16;
    const std::array<int32_t, 9> origins{0, 8, -8, pagePixels - 8, 2 * pagePixels - 8, 3 * pagePixels - 8, 4 * pagePixels - 8, 0, 0};
    for (size_t i = 0; i < origins.size(); ++i) {
        for (uint32_t y = 0; y < Grid2D::Height; ++y)
            for (uint32_t x = 0; x < Grid2D::Width; ++x) {
                const auto layer = uint32_t((y + x + i) % 4);
                expectedCells[size_t(y) * Grid2D::Width + x] = layer;
                input->GetWorld().SetCell(x, y, {{10 * layer}});
            }
        input->View.OriginPixels = {i == 2 ? -8 : 0, origins[i]};
        input->View.CellSizePixels = i == 7 ? std::array<uint32_t, 2>{32, 32} : std::array<uint32_t, 2>{16, 16};
        expected.View = input->View;
        expected.Post = input->Post;
        const auto beforePublish = publications;
        auto token = Draw(engine);
        Require(publications == beforePublish + 1, "Ready frame did not publish exactly once");
        Require(input->GetWorld().ReadState().Cells.data() == (publications % 2 ? recycledStorage : initialStorage), "gameplay storage did not alternate");
        const auto published = policy->PublishedFrame();
        Require(std::equal(published.Cells.begin(), published.Cells.end(), expectedCells.begin()), "gameplay identity was not mapped into the independent texture fixture");
        engine.GetRHI()->WaitIdle();
        VerifyColor(expected, policy->SceneColor(token.Slot));
        if (i == 8) {
            const auto beforePost = publications;
            const auto beforeSubmit = policy->SubmittedFrames();
            const auto *beforeStorage = input->GetWorld().ReadState().Cells.data();
            VerifyPostShader(engine, policy->SceneColor(token.Slot));
            Require(publications == beforePost && policy->SubmittedFrames() == beforeSubmit && input->GetWorld().ReadState().Cells.data() == beforeStorage, "shader-only frame advanced gameplay publication");
        }
    }
    // A change after FrameBegin cannot affect this frame's uploaded snapshot.
    input->View.OriginPixels = {0, 0};
    input->GetWorld().Fill({{10}});
    std::fill(expectedCells.begin(), expectedCells.end(), 1);
    expected.View = input->View;
    expected.Post = input->Post;
    RHIFrameBeginResult delayed;
    for (int attempt = 0; attempt < 100 && !delayed.Ready; ++attempt) {
        engine.GetPlatform()->PollEvents();
        delayed = engine.FrameBegin();
        if (!delayed.Ready)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(delayed.Ready, "no frame for snapshot isolation test");
    input->GetWorld().SetCell(0, 0, {{20}});
    engine.GetInput()->Dispatch();
    engine.Tick(0, delayed.Token);
    engine.FrameEnd(delayed.Token);
    Require(policy->PublishedFrame().Cells[0] == 1, "same-frame gameplay edit leaked into Render");
    engine.GetRHI()->WaitIdle();
    VerifyColor(expected, policy->SceneColor(delayed.Token.Slot));
    expectedCells[0] = 2; // The edit after publication becomes visible only now.
    const auto next = Draw(engine);
    Require(policy->PublishedFrame().Cells[0] == 2 && policy->PublishedFrame().Cells[1] == 1, "next frame lost new or persistent gameplay state");
    engine.GetRHI()->WaitIdle();
    VerifyColor(expected, policy->SceneColor(next.Slot));
    const auto frames = policy->SubmittedFrames();
    Require(policy->UploadedWorldBytes() == frames * Grid2D::UploadBytes, "upload accounting incorrect");
    const auto generation = engine.GetRHI()->GetSwapchainGeneration();
    glfwSetWindowSize(engine.GetPlatform()->GetWindows(), 640, 480);
    RHIFrameToken token{};
    for (int attempt = 0; attempt < 100; ++attempt) {
        token = Draw(engine);
        if (engine.GetRHI()->GetSwapchainGeneration() > generation)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(engine.GetRHI()->GetSwapchainGeneration() > generation, "resize did not recreate the swapchain");
    int width = 0, height = 0;
    engine.GetPlatform()->GetFramebufferSize(width, height);
    Require(policy->SceneColor(token.Slot).Desc.Width == uint32_t(width) && policy->SceneColor(token.Slot).Desc.Height == uint32_t(height), "resize color extent stale");
    engine.GetRHI()->WaitIdle();
    VerifyColor(expected, policy->SceneColor(token.Slot));

    engine.GetPlatform()->OnKeyButtonCallback(GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
    engine.GetInput()->Dispatch();
    Require(input->Post.Values[1] == 0.35f, "input without ImGui failed");
    engine.GetPlatform()->OnKeyButtonCallback(GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
    engine.GetInput()->Dispatch();
    const auto beforeMinimize = policy->SubmittedFrames();
    const auto beforeSkipPublications = publications;
    const auto *beforeSkipStorage = input->GetWorld().ReadState().Cells.data();
    glfwIconifyWindow(engine.GetPlatform()->GetWindows());
    for (int attempt = 0; attempt < 50 && !engine.GetPlatform()->IsWindowMinimized(); ++attempt) {
        engine.GetPlatform()->PollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (engine.GetPlatform()->IsWindowMinimized()) {
        Require(!engine.FrameBegin().Ready, "minimized frame should be skipped");
        Require(policy->SubmittedFrames() == beforeMinimize && publications == beforeSkipPublications && input->GetWorld().ReadState().Cells.data() == beforeSkipStorage, "minimized frame published/uploaded/submitted");
    } else {
        std::cout << "Minimize check skipped: window manager did not report iconification\n";
    }
    input->GetWorld().Fill({{20}});
    std::fill(expectedCells.begin(), expectedCells.end(), 2);
    expected.View = input->View;
    expected.Post = input->Post;
    glfwRestoreWindow(engine.GetPlatform()->GetWindows());
    token = Draw(engine);
    engine.GetRHI()->WaitIdle();
    VerifyColor(expected, policy->SceneColor(token.Slot));
    Require(policy->PublishedFrame().Cells.front() == 2 && policy->PublishedFrame().Cells.back() == 2, "restore did not publish latest gameplay state");
    const auto beforeEnd = publications;
    engine.End();
    engine.End();
    Require(publications == beforeEnd, "shutdown invoked gameplay publisher");
    app.Engine.reset();
    std::cout << "Pixel2D GPU lifecycle " << iteration << " passed: grid pages, full textures, alpha, zoom/crop, post tint, resize/minimize, no-UI input\n";
}
void VerifyDefaultWorldField(const pixel::World &world) {
    const auto state = world.ReadState();
    Require(world.GetWind().IsEnabled(), "default world has no dynamic wind");
    size_t stableAirCells = 0;
    for (uint32_t y = 0; y < state.Height; ++y)
        for (uint32_t x = 0; x < state.Width; ++x) {
            const auto cell = state.Cells[size_t(y) * state.Width + x];
            const auto force = cell.Force;
            // User-facing height increases upward from the bottom of the world.
            const uint32_t height = state.Height - 1 - y;
            Require(cell.Type == (height < 512 ? pixel::CellTypes::Dirt : pixel::CellTypes::Empty), "terrain boundary does not match height above the world bottom");
            Require(force == pixel::ForceDirection{0, 1}, "wind changed the base gravity field");
            if (height < 600) {
                Require(world.GetWind().Sample(float(x) + 0.5f, float(y) + 0.5f) == pixel::WindVelocity{}, "wind leaked below height 600");
                if (height >= 512)
                    ++stableAirCells;
            }
        }
    Require(world.GetWind().Sample(512.5f, 423.5f).X > 0, "wind does not reach height 600");
    Require(stableAirCells == size_t(600 - 512) * state.Width, "default world does not have 88 stable sky rows");
}
void BasicWorldGameplay() {
    Axiom app;
    app.Config = GameConfig::Load();
    auto game = std::make_unique<PixelGame>();
    auto *input = game.get();
    PixelRender *policy = nullptr;
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.EnableUI = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi) {
        auto result = std::make_unique<PixelRender>(rhi, PixelGame::MakeTextures(), input->CreateFramePublisher());
        policy = result.get();
        return result;
    };
    app.Engine = std::make_unique<GameEngine>(std::move(game), CreateRHI(), std::move(setup));
    auto &engine = *app.Engine;
    engine.EngineInit();
    engine.Begin();
    auto &world = input->GetWorld();
    VerifyDefaultWorldField(world);
    const auto initialWind = world.GetWind().Sample(512.5f, 200.5f);
    std::vector<uint32_t> expectedCells(Grid2D::CellCount);
    std::fill(expectedCells.begin() + 512 * Grid2D::Width, expectedCells.end(), 1);
    size_t gameplayFrame = 0;
    auto verify = [&] {
        ++gameplayFrame;
        const auto token = Draw(engine);
        engine.GetRHI()->WaitIdle();
        const auto cells = policy->PublishedFrame().Cells;
        const auto mismatch = std::mismatch(cells.begin(), cells.end(), expectedCells.begin());
        if (mismatch.first != cells.end())
            throw std::runtime_error("gameplay publication differs at frame=" + std::to_string(gameplayFrame)
                                     + " cell=" + std::to_string(mismatch.first - cells.begin())
                                     + " expected=" + std::to_string(*mismatch.second) + " actual=" + std::to_string(*mismatch.first));
        VerifyColor({expectedCells, input->View, input->Post}, policy->SceneColor(token.Slot), PixelGame::MakeTextures());
    };
    verify();
    // Exercise the real default field: empty sky must not disturb the terrain.
    for (int i = 0; i < 5; ++i)
        input->Tick(1.f / 60);
    verify();
    // Observe a block in the actual default wind and verify the GPU publication.
    const auto originalView = input->View;
    input->View.OriginPixels = {500 * 16, 190 * 16};
    const auto oldWind = world.GetWind().Sample(512.5f, 200.5f);
    world.PlaceDirt(512, 200);
    input->Tick(1.f / 60);
    const uint32_t windRow = world.GetCell(513, 200).Type == pixel::CellTypes::Dirt ? 200 : 201;
    Require(world.GetCell(512, 200).Type == pixel::CellTypes::Empty && world.GetCell(513, windRow).Type == pixel::CellTypes::Dirt,
            "default wind did not move an upper-sky block to the right");
    Require(world.GetWind().Sample(512.5f, 200.5f) != oldWind, "default wind did not evolve");
    expectedCells[windRow * Grid2D::Width + 513] = 1;
    verify();
    world.RemoveDirt(513, windRow);
    expectedCells[windRow * Grid2D::Width + 513] = 0;
    input->View = originalView;
    auto *platform = engine.GetPlatform();
    auto moveCursorToCell = [&](uint32_t x, uint32_t y) {
        int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetWindowSize(platform->GetWindows(), &ww, &wh);
        platform->GetFramebufferSize(fw, fh);
        const double px = (x + .5) * input->View.CellSizePixels[0] - input->View.OriginPixels[0];
        const double py = (y + .5) * input->View.CellSizePixels[1] - input->View.OriginPixels[1];
        Require(px >= 0 && py >= 0 && px < fw && py < fh, "test clicked a non-visible cell");
        glfwSetCursorPos(platform->GetWindows(), px * ww / fw, py * wh / fh);
    };
    auto queueClick = [&](uint32_t x, uint32_t y, int button) {
        moveCursorToCell(x, y);
        platform->OnButtonCallback(button, GLFW_PRESS, 0);
        platform->OnButtonCallback(button, GLFW_RELEASE, 0);
    };
    auto clickCell = [&](uint32_t x, uint32_t y, int button) {
        queueClick(x, y, button);
        engine.GetInput()->Dispatch();
    };
    // Multiple clicks and later cursor motion may arrive in one PollEvents batch.
    queueClick(512, 508, GLFW_MOUSE_BUTTON_LEFT);
    queueClick(516, 508, GLFW_MOUSE_BUTTON_LEFT);
    moveCursorToCell(520, 508);
    engine.GetInput()->Dispatch();
    Require(world.GetCell(512, 508).Type == pixel::CellTypes::Dirt && world.GetCell(516, 508).Type == pixel::CellTypes::Dirt
                && world.GetCell(520, 508).Type == pixel::CellTypes::Empty,
            "batched clicks used the later cursor position");
    expectedCells[508 * Grid2D::Width + 512] = expectedCells[508 * Grid2D::Width + 516] = 1;
    verify();
    queueClick(512, 508, GLFW_MOUSE_BUTTON_RIGHT);
    queueClick(516, 508, GLFW_MOUSE_BUTTON_RIGHT);
    moveCursorToCell(520, 508);
    engine.GetInput()->Dispatch();
    Require(world.GetCell(512, 508).Type == pixel::CellTypes::Empty && world.GetCell(516, 508).Type == pixel::CellTypes::Empty,
            "batched removals used the later cursor position");
    expectedCells[508 * Grid2D::Width + 512] = expectedCells[508 * Grid2D::Width + 516] = 0;
    verify();
    clickCell(512, 508, GLFW_MOUSE_BUTTON_LEFT);
    Require(world.GetCell(512, 508).Type == pixel::CellTypes::Dirt && world.GetCell(512, 507).Type == pixel::CellTypes::Empty, "left click missed its single cell");
    expectedCells[508 * Grid2D::Width + 512] = 1;
    verify();
    for (int i = 0; i < 5; ++i)
        input->Tick(1.f / 60);
    expectedCells[508 * Grid2D::Width + 512] = 0;
    expectedCells[511 * Grid2D::Width + 512] = 1;
    verify();
    clickCell(512, 512, GLFW_MOUSE_BUTTON_RIGHT);
    Require(world.GetCell(512, 512).Type == pixel::CellTypes::Empty, "right click did not remove dirt");
    input->Tick(1.f / 60);
    expectedCells[511 * Grid2D::Width + 512] = 0;
    verify();
    // Zoom and pan, then verify the event-to-cell mapping on the actual HiDPI window.
    input->OnKeyEvent(Key::KEY_EQUAL, 0, ButtonAction::ACTION_PRESS, Mod{});
    input->View.OriginPixels[0] += 16;
    input->View.OriginPixels[1] -= 16;
    clickCell(514, 510, GLFW_MOUSE_BUTTON_LEFT);
    expectedCells[510 * Grid2D::Width + 514] = 1;
    verify();
    clickCell(514, 510, GLFW_MOUSE_BUTTON_RIGHT);
    expectedCells[510 * Grid2D::Width + 514] = 0;
    verify();
    world.FillForceDirection({0, 0});
    world.AddWindImpulse(512, 200, 64, {0, -120});
    input->OnKeyEvent(Key::KEY_R, 0, ButtonAction::ACTION_PRESS, Mod{});
    VerifyDefaultWorldField(world);
    Require(world.GetWind().Sample(512.5f, 200.5f) == initialWind, "R reset retained wind time or impulse state");
    verify();
    // A block at height 599 must cross the entire stable sky band vertically.
    world.PlaceDirt(520, 424);
    for (int i = 0; i < 87; ++i)
        input->Tick(1.f / 60);
    expectedCells[511 * Grid2D::Width + 520] = 1;
    verify();
    input->Tick(1.f / 60);
    verify();
    world.RemoveDirt(520, 511);
    expectedCells[511 * Grid2D::Width + 520] = 0;
    // Follow a spatial field that turns right, then up, then holds still.
    world.SetForceDirection(512, 508, {1, 0});
    world.SetForceDirection(513, 508, {0, -1});
    world.SetForceDirection(513, 507, {0, 0});
    clickCell(512, 508, GLFW_MOUSE_BUTTON_LEFT);
    input->Tick(1.f / 60);
    expectedCells[508 * Grid2D::Width + 513] = 1;
    verify();
    input->Tick(1.f / 60);
    expectedCells[508 * Grid2D::Width + 513] = 0;
    expectedCells[507 * Grid2D::Width + 513] = 1;
    verify();
    input->Tick(1.f / 60);
    verify();
    Require(world.GetCell(512, 508).Force == pixel::ForceDirection{1, 0} && world.GetCell(513, 508).Force == pixel::ForceDirection{0, -1}, "moving material changed the spatial field");
    clickCell(513, 507, GLFW_MOUSE_BUTTON_RIGHT);
    expectedCells[507 * Grid2D::Width + 513] = 0;
    verify();
    Require(world.GetCell(513, 507).Force == pixel::ForceDirection{0, 0}, "mouse removal cleared the force field");
    engine.End();
    app.Engine.reset();
    std::cout << "Basic world GPU gameplay passed: terrain, clicks, falling/stacking, HiDPI zoom/pan, reset, force-field turns and dynamic upper-sky wind\n";
}
std::vector<uint32_t> ExpectedFieldColors(const pixel::World &world) {
    // Independent oracle: use the base force and wind directly, not the
    // presentation converter or its shared simulation sampling helper.
    std::vector<uint32_t> colors(Grid2D::CellCount);
    for (uint32_t y = 0; y < Grid2D::Height; ++y)
        for (uint32_t x = 0; x < Grid2D::Width; ++x) {
            const auto base = world.GetCell(x, y).Force;
            const auto wind = world.GetWind().Sample(float(x) + 0.5f, float(y) + 0.5f);
            const double vx = float(base.X) * 60 + wind.X, vy = float(base.Y) * 60 + wind.Y;
            const double length = std::sqrt(vx * vx + vy * vy);
            const auto r = uint32_t(std::lround(255 * (length == 0 ? 0.5 : (vx / length + 1) / 2)));
            const auto g = uint32_t(std::lround(255 * (length == 0 ? 0.5 : (vy / length + 1) / 2)));
            colors[size_t(y) * Grid2D::Width + x] = r | (g << 8) | (128 << 16);
        }
    return colors;
}

void ForceFieldDebugGameplay() {
    Axiom app;
    app.Config = GameConfig::Load();
    auto game = std::make_unique<PixelGame>();
    auto *input = game.get();
    PixelRender *policy = nullptr;
    RenderSetup setup;
    setup.Enable3DScene = setup.EnableUI = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi) {
        auto result = std::make_unique<PixelRender>(rhi, PixelGame::MakeTextures(), input->CreateFramePublisher());
        policy = result.get();
        return result;
    };
    app.Engine = std::make_unique<GameEngine>(std::move(game), CreateRHI(), std::move(setup));
    auto &engine = *app.Engine;
    engine.EngineInit();
    engine.Begin();
    auto &world = input->GetWorld();
    world.DisableWind();
    constexpr std::array<pixel::ForceDirection, 9> directions{{{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {-1, -1}, {1, 1}, {-1, 1}, {1, -1}}};
    constexpr std::array<uint32_t, 9> colors{0x808080, 0x8080FF, 0x808000, 0x80FF80, 0x800080, 0x802525, 0x80DADA, 0x80DA25, 0x8025DA};
    std::vector<uint32_t> expected(Grid2D::CellCount);
    for (uint32_t y = 0; y < Grid2D::Height; ++y)
        for (uint32_t x = 0; x < Grid2D::Width; ++x) {
            const auto stripe = (x / 8) % directions.size();
            world.SetForceDirection(x, y, directions[stripe]);
            expected[size_t(y) * Grid2D::Width + x] = colors[stripe];
        }
    const std::vector<pixel::CellState> initial(world.ReadState().Cells.begin(), world.ReadState().Cells.end());
    auto press = [&](Key key) { input->OnKeyEvent(key, 0, ButtonAction::ACTION_PRESS, Mod{}); };
    input->OnKeyEvent(Key::KEY_F1, 0, ButtonAction::ACTION_REPEAT, Mod{});
    input->OnKeyEvent(Key::KEY_F1, 0, ButtonAction::ACTION_RELEASE, Mod{});
    Require(input->DisplayMode == GridDisplayMode::Materials, "F1 repeat/release changed the display mode");
    press(Key::KEY_SPACE);
    press(Key::KEY_F1);
    size_t debugFrame = 0;
    auto verifyField = [&](int tolerance = 0) {
        ++debugFrame;
        const auto token = Draw(engine);
        engine.GetRHI()->WaitIdle();
        const auto frame = policy->PublishedFrame();
        Require(frame.DisplayMode == GridDisplayMode::ForceField && frame.Post.Values == PostProcessParameters{}.Values, "debug frame mode/tint is wrong");
        for (size_t i = 0; i < expected.size(); ++i)
            for (size_t channel = 0; channel < 3; ++channel) {
                const int actual = int((frame.Cells[i] >> (channel * 8)) & 255);
                const int wanted = int((expected[i] >> (channel * 8)) & 255);
                if (std::abs(actual - wanted) > tolerance)
                    throw std::runtime_error("debug publication differs at frame=" + std::to_string(debugFrame)
                                             + " cell=" + std::to_string(i) + " expected=" + std::to_string(wanted) + " actual=" + std::to_string(actual));
            }
        VerifyColor({expected, input->View, {}, GridDisplayMode::ForceField}, policy->SceneColor(token.Slot));
    };
    for (int page = 0; page < int(Grid2D::PageCount); ++page) {
        input->View.OriginPixels = {-8, (page * int(Grid2D::PageHeight) - 2) * 16};
        verifyField(); // Includes page seams, outside-world pixels, air and dirt.
    }
    press(Key::KEY_MINUS);
    verifyField();
    Require(std::equal(initial.begin(), initial.end(), world.ReadState().Cells.begin()), "debug view changed simulation state");
    press(Key::KEY_F1);
    const auto materialToken = Draw(engine);
    engine.GetRHI()->WaitIdle();
    Require(policy->PublishedFrame().DisplayMode == GridDisplayMode::Materials && policy->PublishedFrame().Post.Values == input->Post.Values
                && input->Post.Values[1] == 0.35f,
            "leaving debug view did not restore material mode and tint");
    for (size_t i = 0; i < expected.size(); ++i)
        expected[i] = initial[i].Type == pixel::CellTypes::Dirt ? 1 : 0;
    VerifyColor({expected, input->View, input->Post}, policy->SceneColor(materialToken.Slot), PixelGame::MakeTextures());

    press(Key::KEY_F1);
    press(Key::KEY_R);
    Require(input->DisplayMode == GridDisplayMode::ForceField, "world reset unexpectedly disabled debug view");
    input->View.CellSizePixels = {1, 1};
    input->View.OriginPixels = {0, 0};
    expected = ExpectedFieldColors(world);
    // The independent double-precision oracle can differ by one RGB8 level
    // from the production float colour at rounding thresholds. Fixtures above
    // remain exact; this tolerance applies only to continuous simulated wind.
    verifyField(1);
    const auto beforeWind = expected;
    world.AddWindImpulse(512, 200, 100, {-120, -120});
    input->Tick(0.1f);
    expected = ExpectedFieldColors(world);
    Require(expected != beforeWind, "dynamic wind did not update field colours");
    for (size_t i = size_t(424) * Grid2D::Width; i < expected.size(); ++i)
        Require(expected[i] == 0x80FF80, "debug wind colour leaked into the gravity region");
    verifyField(1);
    // Compare a dirt patch against an otherwise identical empty-sky world.
    // This exercises actual gameplay coupling rather than a test-only impulse.
    press(Key::KEY_R);
    pixel::World clearSky(Grid2D::Width, Grid2D::Height);
    clearSky.GenerateTerrain(512);
    clearSky.ConfigureWind(424);
    for (uint32_t y = 160; y < 192; ++y)
        for (uint32_t x = 480; x < 544; ++x)
            world.PlaceDirt(x, y);
    for (int step = 0; step < 12; ++step) {
        input->Tick(1.f / 60);
        clearSky.Tick(1.f / 60);
    }
    float largestFeedback = 0;
    for (uint32_t y = 144; y < 224; ++y)
        for (uint32_t x = 448; x < 576; ++x) {
            const auto coupled = world.GetWind().Sample(float(x) + 0.5f, float(y) + 0.5f);
            const auto reference = clearSky.GetWind().Sample(float(x) + 0.5f, float(y) + 0.5f);
            largestFeedback = std::max(largestFeedback, std::abs(coupled.X - reference.X) + std::abs(coupled.Y - reference.Y));
        }
    Require(largestFeedback > 1, "actual dirt did not change the wind relative to an empty-sky control");
    expected = ExpectedFieldColors(world);
    Require(expected != ExpectedFieldColors(clearSky), "dirt feedback is absent from the F1 colour view");
    input->View.OriginPixels = {448 * 8, 144 * 8};
    input->View.CellSizePixels = {8, 8};
    verifyField(1);
    press(Key::KEY_R);
    clearSky.GenerateTerrain(512);
    clearSky.ConfigureWind(424);
    // A narrow stack rising from the terrain reproduces the reported case at
    // production scale, including erosion under the default moving wind.
    for (uint32_t y = 344; y < 512; ++y)
        for (uint32_t x = 512; x < 516; ++x)
            world.PlaceDirt(x, y);
    for (int step = 0; step < 60; ++step) {
        input->Tick(1.f / 60);
        clearSky.Tick(1.f / 60);
    }
    const auto coupled = world.GetFieldVelocity(511, 410);
    const auto reference = clearSky.GetFieldVelocity(511, 410);
    const double angle = std::abs(std::atan2(double(coupled.X) * reference.Y - double(coupled.Y) * reference.X,
                                             double(coupled.X) * reference.X + double(coupled.Y) * reference.Y));
    Require(angle > 0.1, "narrow dirt stack did not visibly deflect the default combined field");
    expected = ExpectedFieldColors(world);
    input->View.OriginPixels = {448 * 8, 312 * 8};
    verifyField(1);
    press(Key::KEY_R);
    clearSky.GenerateTerrain(512);
    clearSky.ConfigureWind(424);
    expected = ExpectedFieldColors(clearSky);
    verifyField(1);
    engine.End();
    app.Engine.reset();
    std::cout << "Force-field debug GPU view passed: signed/zero directions, pages, F1/tint, reset, dynamic wind, dirt patch and narrow stack\n";
}
} // namespace
void RunPixelDebugUISmoke();
int main() {
    try {
        BasicWorldGameplay();
        ForceFieldDebugGameplay();
        RunPixelDebugUISmoke();
        Lifecycle(1);
        Lifecycle(2);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Pixel2D GPU smoke failed: " << error.what() << '\n';
        return 1;
    }
}
