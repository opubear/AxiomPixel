#include "../Support/TestAccess.h"
#include "../Support/SimulatedWorld.h"
#include "PixelSmokeApplication.h"
#include "Engine.h"
#include "HAL/Platform/PlatformInterface.h"
#include "HAL/Shader/ShaderLibrary.h"
#include "HAL/Vulkan/VulkanRasterResources.h"
#include "InputManager.h"
#include "Application/PixelGame.h"
#include "Gameplay/FormulaBridge/ProcessExample.h"
#include "Asset/Loaders/AssetLoader.h"
#include "Assets/ArtAssets.h"
#include "PixelRender/BackgroundView.h"
#include "PixelRender/PixelRender.h"
#include "SystemEnum.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace {
using World = pixel::testing::SimulatedWorld;
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
            const bool nearCharacter = frame.Character && x + 2 >= frame.Character->Bounds[0] && x <= frame.Character->Bounds[0] + frame.Character->Bounds[2] + 2
                                       && y + 2 >= frame.Character->Bounds[1] && y <= frame.Character->Bounds[1] + frame.Character->Bounds[3] + 2;
            if (!nearCharacter && x > 32 && y > 32 && x + 1 != extent.Width && y + 1 != extent.Height && (x * 13 + y * 7) % 97 != 0)
                continue;
            const auto localOrigin = LocalGridPixelOrigin(frame);
            const int64_t qx = int64_t(localOrigin[0]) + x;
            const int64_t qy = int64_t(localOrigin[1]) + y;
            auto expected = frame.View.Background;
            const auto cw = frame.View.CellSizePixels[0], ch = frame.View.CellSizePixels[1];
            if (qx >= 0 && qy >= 0 && qx < int64_t(frame.Width) * cw && qy < int64_t(frame.Height) * ch) {
                const auto id = frame.Cells[size_t(qy / ch) * frame.Width + size_t(qx / cw)];
                if (id == Grid2D::MissingCell) {
                    const float shade = ((qx / cw / 4 + qy / ch / 4) & 1) ? .22f : .30f;
                    expected = {shade, shade, shade, 1};
                } else if (frame.DisplayMode == GridDisplayMode::ForceField) {
                    for (size_t c = 0; c < 3; ++c)
                        expected[c] = float((id >> (8 * c)) & 255) / 255;
                } else if (id) {
                    const auto phase = [&](int64_t cell) { return (cell % catalog.CellsPerTile + catalog.CellsPerTile) % catalog.CellsPerTile; };
                    const double fx = (double(qx % cw) + .5) / cw, fy = (double(qy % ch) + .5) / ch;
                    const double u = (double((qx / cw % catalog.CellsPerTile + phase(frame.CellOrigin[0])) % catalog.CellsPerTile) + fx) / catalog.CellsPerTile;
                    double v = (double((qy / ch % catalog.CellsPerTile + phase(frame.CellOrigin[1])) % catalog.CellsPerTile) + fy) / catalog.CellsPerTile;
                    if (id & Grid2D::SurfaceCellFlag)
                        v = (double((id & Grid2D::SurfaceStripMask) >> Grid2D::SurfaceStripShift) + fy) / 8;
                    const size_t tx = std::min<size_t>(catalog.Width - 1, size_t(u * catalog.Width));
                    const size_t ty = std::min<size_t>(catalog.Height - 1, size_t(v * catalog.Height));
                    const size_t texel = (((id & Grid2D::MaterialLayerMask) * catalog.Height + ty) * catalog.Width + tx) * 4;
                    const float alpha = float(std::to_integer<uint8_t>(catalog.Pixels[texel + 3])) / 255;
                    for (size_t c = 0; c < 3; ++c)
                        expected[c] = Linear(std::to_integer<uint8_t>(catalog.Pixels[texel + c])) * alpha + expected[c] * (1 - alpha);
                }
            }
            if (frame.Character) {
                const auto &sprite = *frame.Character;
                const auto &bounds = sprite.Bounds;
                const double left = bounds[0], top = bounds[1], right = left + bounds[2], bottom = top + bounds[3];
                const double split = top + bounds[3] / 4.0;
                const double horizontal = std::max(0.0, std::min(double(x + 1), right) - std::max(double(x), left));
                const double head = horizontal * std::max(0.0, std::min(double(y + 1), split) - std::max(double(y), top));
                const double body = horizontal * std::max(0.0, std::min(double(y + 1), bottom) - std::max(double(y), split));
                for (size_t c = 0; c < 3; ++c)
                    expected[c] = float(expected[c] * (1 - head - body) + sprite.HeadColor[c] * head + sprite.BodyColor[c] * body);
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
// The oracle remains a complete independent finite-world fixture. Only its
// visible region is projected into the render-owned atlas for comparison.
std::vector<uint32_t> ExpectedRegion(std::span<const uint32_t> cells, WorldGridFrameView published) {
    std::vector<uint32_t> result(size_t(published.Width) * published.Height, Grid2D::MissingCell);
    for (uint32_t y = 0; y < published.Height; ++y)
        for (uint32_t x = 0; x < published.Width; ++x) {
            const auto wx = published.CellOrigin[0] + x, wy = published.CellOrigin[1] + y;
            if (wx >= 0 && wy >= 0 && wx < Grid2D::Width && wy < Grid2D::Height)
                result[size_t(y) * published.Width + x] = cells[size_t(wy) * Grid2D::Width + size_t(wx)];
        }
    return result;
}
void CheckPublishedRegion(WorldGridFrameView published, std::span<const uint32_t> expected, int tolerance = 0) {
    const auto region = ExpectedRegion(expected, published);
    Require(region.size() == published.Cells.size(), "published region has incomplete storage");
    for (size_t i = 0; i < region.size(); ++i) {
        if (region[i] == Grid2D::MissingCell || published.Cells[i] == Grid2D::MissingCell) {
            Require(region[i] == published.Cells[i], "published missing-region mask differs from finite-world bounds");
            continue;
        }
        for (size_t channel = 0; channel < 3; ++channel)
            if (std::abs(int((region[i] >> (channel * 8)) & 255) - int((published.Cells[i] >> (channel * 8)) & 255)) > tolerance)
                throw std::runtime_error("published region differs from the independent world fixture at (" +
                    std::to_string(published.CellOrigin[0] + int64_t(i % published.Width)) + "," +
                    std::to_string(published.CellOrigin[1] + int64_t(i / published.Width)) + "): expected=" +
                    std::to_string(region[i]) + " actual=" + std::to_string(published.Cells[i]));
    }
}
void VerifyPublishedColor(WorldGridFrameView oracle, WorldGridFrameView published, const RHITexture &texture,
                          const GridTextureCatalog &catalog = Textures()) {
    const auto cells = ExpectedRegion(oracle.Cells, published);
    oracle.Cells = cells;
    oracle.Width = published.Width;
    oracle.Height = published.Height;
    oracle.CellOrigin = published.CellOrigin;
    VerifyColor(oracle, texture, catalog);
}
uint32_t PublishedCell(WorldGridFrameView frame, int64_t x, int64_t y) {
    Require(x >= frame.CellOrigin[0] && y >= frame.CellOrigin[1] && x - frame.CellOrigin[0] < frame.Width && y - frame.CellOrigin[1] < frame.Height,
            "fixture cell is outside the published region");
    return frame.Cells[size_t(y - frame.CellOrigin[1]) * frame.Width + size_t(x - frame.CellOrigin[0])];
}
std::vector<pixel::CellState> CopyWorldCells(const pixel::World &world) {
    const auto state = world.ReadState();
    std::vector<pixel::CellState> result(state.CellCount());
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = state.AtIndex(i);
    return result;
}
RHIFrameToken Draw(Axiom &app) {
    auto &engine = *app.Engine;
    for (int attempt = 0; attempt < 100; ++attempt) {
        engine.GetPlatform()->PollEvents();
        if (!app.FrameBegin()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        const auto token = engine.GetFrameState().Token;
        engine.GetInput()->Dispatch();
        app.Tick(0);
        app.FrameEnd();
        return token;
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
void WindVisualizationGpu() {
    auto game = std::make_unique<PixelGame>(false);
    WindVisualSettings settings;
    settings.Mode = WindVisualMode::ShortTrails;
    float time = .125f;
    float velocity = 90;
    float sky = .18f;
    bool textured = false, character = false, opposing = false;
    GridDisplayMode display = GridDisplayMode::Materials;
    GridView2D view;
    view.CellSizePixels = {8, 8};
    view.OriginPixels = {64 * 8, 64 * 8};
    view.WorldOriginCells = {};
    PixelRender *policy = nullptr;
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi, IRenderSceneInterface *) {
        auto render = std::make_unique<PixelRender>(rhi, Textures(), [&](WorldGridFrame &frame) {
            frame.SetRegion({0, 0}, Grid2D::Width, Grid2D::Height);
            frame.View = view;
            frame.View.Background = {sky, sky, sky, 1};
            frame.Wind = settings;
            frame.WindTimeSeconds = time;
            frame.HasWind = true; // Exercise shader calm handling as well as CPU early-out.
            frame.DisplayMode = display;
            frame.Character = character ? std::optional{CharacterDraw{{80, 80, 32, 64}, {1, .5f, .25f, 1}, {0, 1, .5f, 1}}} : std::nullopt;
            for (uint32_t y = 0; y < Grid2D::Height; ++y)
                for (uint32_t x = 0; x < Grid2D::Width; ++x) {
                    const size_t i = size_t(y) * Grid2D::Width + x;
                    // A fixed opaque square and calm lower band validate masks.
                    const bool solid = x >= 84 && x < 92 && y >= 84 && y < 92;
                    frame.Cells()[i] = display == GridDisplayMode::ForceField ? 0x8080FF : (solid ? 1 : (textured ? 1 + (x / 2 + y / 2) % 2 : 0));
                    const float localVelocity = opposing && x < 100 ? -velocity : velocity;
                    frame.WindCells()[i] = solid ? WindSolidTexel : PackWindVelocity(y >= 116 ? 0 : localVelocity, 0);
                }
        });
        policy = render.get();
        return render;
    };
    PixelSmokeApplication app(std::move(game));
    app.Engine->SetRender(std::move(setup));
    auto &engine = *app.Engine;
    app.Initialize(); app.Begin();
    auto capture = [&] {
        const auto token = Draw(app);
        engine.GetRHI()->WaitIdle();
        return std::pair{ReadColor(policy->WorldColor(token.Slot)), ReadColor(policy->SceneColor(token.Slot))};
    };
    auto changedPixels = [](const auto &a, const auto &b) {
        size_t changed = 0;
        for (size_t i = 0; i < a.size(); i += 4)
            if (std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) + std::abs(int(a[i + 2]) - int(b[i + 2])) > 3) ++changed;
        return changed;
    };
    const auto extent = engine.GetRHI()->GetPresentationExtent();
    auto save = [&](const char *name, const std::vector<uint8_t> &rgba) {
        const char *directory = std::getenv("PIXEL_WIND_CAPTURE_DIR");
        if (!directory) return;
        std::filesystem::create_directories(directory);
        std::ofstream file(std::filesystem::path(directory) / name, std::ios::binary);
        file << "P6\n" << extent.Width << ' ' << extent.Height << "\n255\n";
        // Readback is linear RGBA8; encode sRGB for a faithful display preview.
        for (size_t i = 0; i < rgba.size(); i += 4)
            for (size_t c = 0; c < 3; ++c) {
                const float linear = rgba[i + c] / 255.f;
                const auto srgb = uint8_t(std::lround(255 * (linear <= .0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.f / 2.4f) - .055f)));
                file.put(char(srgb));
            }
    };
    for (auto mode : {WindVisualMode::ShortTrails, WindVisualMode::AirRefraction}) {
        settings.Mode = mode;
        settings.RefractionPixels = 12;
        for (float background : {.04f, .5f, .95f}) {
            sky = background;
            auto [world, scene] = capture();
            Require(changedPixels(world, scene) > 500, "wind invisible on a flat dark/grey/bright background");
            for (uint32_t y = 0; y < extent.Height; ++y)
                for (uint32_t x = 0; x < extent.Width; ++x) {
                    const bool solid = x >= 160 && x < 224 && y >= 160 && y < 224;
                    const bool calm = y >= 424; // Includes a one-cell guard beyond the boundary.
                    if (!solid) {
                        const size_t i = (size_t(y) * extent.Width + x) * 4;
                        Require(std::abs(int(scene[i]) - int(scene[i + 1])) <= 1 && std::abs(int(scene[i + 1]) - int(scene[i + 2])) <= 1,
                                "refraction bilinear footprint pulled coloured solid into neutral air");
                    }
                    if (solid || calm)
                        for (size_t c = 0; c < 4; ++c)
                            Require(scene[(size_t(y) * extent.Width + x) * 4 + c] == world[(size_t(y) * extent.Width + x) * 4 + c], "wind alters solid or calm pixels");
                }
            if (background == .04f) save(mode == WindVisualMode::ShortTrails ? "trails.ppm" : "refraction.ppm", scene);
        }
        sky = .18f;
        time = .125f;
        auto first = capture().second;
        time += 1.f / 120;
        auto next = capture().second;
        Require(changedPixels(first, next) > 100, "wind animation is static");
        // A uniform +X field must transport the pattern to the right.
        auto error = [&](int shift) {
            uint64_t total = 0;
            for (uint32_t y = 24; y < std::min(144u, extent.Height); ++y)
                for (uint32_t x = 24; x < std::min(600u, extent.Width - 24); ++x) {
                    const size_t a = (size_t(y) * extent.Width + x) * 4;
                    const size_t b = (size_t(y) * extent.Width + x + shift) * 4;
                    total += uint64_t(std::abs(int(first[a]) - int(next[b])));
                }
            return total;
        };
        Require(error(1) < error(-1), "wind moves against positive X velocity");
        Require(capture().second == next, "unchanged time is not deterministic across frame slots");
        velocity = 0;
        auto calm = capture();
        Require(calm.first == calm.second, "zero wind produces an effect");
        velocity = -90;
        first = capture().second;
        time += 1.f / 120;
        next = capture().second;
        Require(error(-1) < error(1), "negative wind moves in the wrong direction");
        settings.MotionScale = .02f;
        for (float speed : {10.f, 90.f}) {
            velocity = speed;
            time = .5f;
            first = capture().second;
            time += .125f;
            next = capture().second;
            Require(error(1) < error(-1), "slow flow reverses during phase crossfade");
        }
        settings.MotionScale = 0;
        first = capture().second;
        time += .5f;
        Require(capture().second == first, "zero motion scale animates wind");
        settings.MotionScale = .2f;
        velocity = 90;
    }
    settings.Mode = WindVisualMode::AirRefraction;
    textured = true;
    settings.RefractionPixels = 0;
    auto noDisplacement = capture().second;
    settings.RefractionPixels = 8;
    auto refracted = capture().second;
    Require(changedPixels(noDisplacement, refracted) > 500, "refraction never displaces scene colour");
    textured = false;
    character = true;
    auto composed = capture().second;
    for (const auto point : {std::array{90u, 88u, 255u, 128u, 64u}, std::array{90u, 120u, 0u, 255u, 128u}})
        for (size_t c = 0; c < 3; ++c)
            Require(std::abs(int(composed[(size_t(point[1]) * extent.Width + point[0]) * 4 + c]) - int(point[c + 2])) <= 1, "wind distorts the foreground character");
    character = false;
    settings.Mode = WindVisualMode::Off;
    auto off = capture(); Require(off.first == off.second, "Off differs from original scene");
    settings.Mode = WindVisualMode::AirRefraction;
    settings.Opacity = 0;
    off = capture(); Require(off.first == off.second, "zero opacity differs from original scene");
    settings.Opacity = .3f;
    display = GridDisplayMode::ForceField;
    off = capture(); Require(off.first == off.second, "F1 is affected by wind appearance");
    display = GridDisplayMode::Materials;
    view.OriginPixels = {-100000, -100000};
    view.WorldOriginCells = {};
    off = capture(); Require(off.first == off.second, "wind appears outside world bounds");
    // Nonzero nearest texels can cancel exactly at the backtrace endpoint:
    // visual speed = 36/23, age = 23/24, travel = 1.5 cells to x=100.
    opposing = true;
    velocity = 90;
    sky = .18f;
    view.CellSizePixels = {1, 1};
    view.OriginPixels = {64, 64};
    view.WorldOriginCells = {};
    settings.MotionScale = .02f;
    settings.Opacity = 1;
    time = 115.f / 120;
    for (auto mode : {WindVisualMode::ShortTrails, WindVisualMode::AirRefraction}) {
        settings.Mode = mode;
        const auto [world, scene] = capture();
        for (uint32_t y = 0; y < extent.Height; ++y)
            for (uint32_t x : {34u, 37u})
                for (size_t c = 0; c < 3; ++c) {
                    const size_t i = (size_t(y) * extent.Width + x) * 4 + c;
                    // The other phase contributes less than one RGB8 level.
                    Require(std::abs(int(scene[i]) - int(world[i])) <= 1,
                            "opposing wind creates a streak at a calm backtrace endpoint");
                }
    }
    app.Shutdown();
    std::cout << "Wind GPU visualization passed: modes, signed motion, flat backgrounds, actual refraction, calm/solid/outside masks, opposing-wind endpoints, characters, Off/F1 and frame slots\n";
}
void Lifecycle(int iteration) {
    auto game = std::make_unique<PixelGame>(false);
    auto *input = game.get();
    input->GetPresentation().WindVisual.Mode = WindVisualMode::Off;
    input->SetControlMode(PixelGame::ControlMode::DebugCamera);
    pixel::testing::SessionAccess::Actors(input->GetSession()).Clear();
    pixel::testing::SessionAccess::World(input->GetSession()).GenerateTerrain(512); // Texture-only lifecycle fixture has no character.
    PixelRender *policy = nullptr;
    uint64_t publications = 0;
    // Independent fixture oracle: never derive expected pixels from the renderer's
    // converted output. Gameplay types intentionally differ from texture layers.
    std::vector<uint32_t> expectedCells(Grid2D::CellCount);
    for (uint32_t y = 0; y < Grid2D::Height; ++y)
        for (uint32_t x = 0; x < Grid2D::Width; ++x) {
            expectedCells[size_t(y) * Grid2D::Width + x] = 1 + (x / 4 + y / 4) % 3;
            pixel::testing::SessionAccess::World(input->GetSession()).SetCell(x, y, {static_cast<pixel::CellTypeId>(10 * expectedCells[size_t(y) * Grid2D::Width + x])});
        }
    const auto initialContents = CopyWorldCells(pixel::testing::SessionAccess::World(input->GetSession()));
    WorldGridFrameView expected{expectedCells, input->GetView(), input->GetPresentation().Post};
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi, IRenderSceneInterface *) {
        auto publish = input->CreateFramePublisher();
        auto result = std::make_unique<PixelRender>(rhi, Textures(), [&, publish = std::move(publish)](WorldGridFrame &frame) {
            ++publications;
            publish(frame);
        });
        policy = result.get();
        return result;
    };
    PixelSmokeApplication app(std::move(game));
    // Application configuration loads the production JSON appearances. Install
    // this fixture's independent mapping with its matching test texture catalog.
    input->GetPresentation().Appearances = {{10, 1}, {20, 2}, {30, 3}};
    app.Engine->SetRender(std::move(setup));
    auto &engine = *app.Engine;
    app.Initialize();
    app.Begin();
    expected.View = input->GetView();
    Require(publications == 0, "world published before first Ready frame");
    auto first = Draw(app);
    Require(publications == 1, "first frame did not publish initialized world");
    CheckPublishedRegion(policy->PublishedFrame(), expectedCells);
    Require(CopyWorldCells(pixel::testing::SessionAccess::World(input->GetSession())) == initialContents, "snapshot publication changed gameplay storage contents");
    engine.GetRHI()->WaitIdle();
    VerifyPublishedColor(expected, policy->PublishedFrame(), policy->SceneColor(first.Slot));
    Require(!engine.GetScene() && !app.GetDebugUI(), "2D initialized 3D scene or UI");
    {
        auto view = input->GetView();
        view.Background = {.1f, .2f, .3f, 1};
        input->SetView(view);
    }
    const int32_t pagePixels = Grid2D::PageHeight * 16;
    const std::array<int32_t, 9> origins{0, 8, -8, pagePixels - 8, 2 * pagePixels - 8, 3 * pagePixels - 8, 4 * pagePixels - 8, 0, 0};
    for (size_t i = 0; i < origins.size(); ++i) {
        for (uint32_t y = 0; y < Grid2D::Height; ++y)
            for (uint32_t x = 0; x < Grid2D::Width; ++x) {
                const auto layer = uint32_t((y + x + i) % 4);
                expectedCells[size_t(y) * Grid2D::Width + x] = layer;
                pixel::testing::SessionAccess::World(input->GetSession()).SetCell(x, y, {static_cast<pixel::CellTypeId>(10 * layer)});
            }
        {
            auto view = input->GetView();
            view.OriginPixels = {i == 2 ? -8 : 0, origins[i]};
            input->SetView(view);
        }
        {
            auto view = input->GetView();
            view.CellSizePixels = i == 7 ? std::array<uint32_t, 2>{32, 32} : std::array<uint32_t, 2>{16, 16};
            input->SetView(view);
        }
        expected.View = input->GetView();
        expected.Post = input->GetPresentation().Post;
        const auto beforePublish = publications;
        auto token = Draw(app);
        Require(publications == beforePublish + 1, "Ready frame did not publish exactly once");
        const auto published = policy->PublishedFrame();
        CheckPublishedRegion(published, expectedCells);
        engine.GetRHI()->WaitIdle();
        VerifyPublishedColor(expected, policy->PublishedFrame(), policy->SceneColor(token.Slot));
        if (i == 8) {
            const auto beforePost = publications;
            const auto beforeSubmit = policy->SubmittedFrames();
            const auto beforeStorage = CopyWorldCells(pixel::testing::SessionAccess::World(input->GetSession()));
            VerifyPostShader(engine, policy->SceneColor(token.Slot));
            Require(publications == beforePost && policy->SubmittedFrames() == beforeSubmit && CopyWorldCells(pixel::testing::SessionAccess::World(input->GetSession())) == beforeStorage, "shader-only frame advanced gameplay publication");
        }
    }
    // A change after FrameBegin cannot affect this frame's uploaded snapshot.
    {
        auto view = input->GetView();
        view.OriginPixels = {0, 0};
        view.WorldOriginCells = {};
        input->SetView(view);
    }
    pixel::testing::SessionAccess::World(input->GetSession()).Fill({pixel::CellTypeId::Dirt});
    std::fill(expectedCells.begin(), expectedCells.end(), 1);
    expected.View = input->GetView();
    expected.Post = input->GetPresentation().Post;
    RHIFrameBeginResult delayed;
    for (int attempt = 0; attempt < 100 && !delayed.Ready; ++attempt) {
        engine.GetPlatform()->PollEvents();
        app.FrameBegin();
        delayed = engine.GetFrameState();
        if (!delayed.Ready)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(delayed.Ready, "no frame for snapshot isolation test");
    auto movedView = input->GetView();
    movedView.OriginPixels[0] += 32;
    input->SetView(movedView);
    Require(policy->PublishedFrame().View.OriginPixels == expected.View.OriginPixels,
            "same-frame camera movement leaked into published view");
    pixel::testing::SessionAccess::World(input->GetSession()).SetCell(0, 0, {static_cast<pixel::CellTypeId>(20)});
    engine.GetInput()->Dispatch();
    app.Tick(0);
    app.FrameEnd();
    Require(PublishedCell(policy->PublishedFrame(), 0, 0) == 1, "same-frame gameplay edit leaked into Render");
    engine.GetRHI()->WaitIdle();
    VerifyPublishedColor(expected, policy->PublishedFrame(), policy->SceneColor(delayed.Token.Slot));
    input->SetView(expected.View);
    expectedCells[0] = 2; // The edit after publication becomes visible only now.
    const auto next = Draw(app);
    Require(PublishedCell(policy->PublishedFrame(), 0, 0) == 2 && PublishedCell(policy->PublishedFrame(), 1, 0) == 1, "next frame lost new or persistent gameplay state");
    engine.GetRHI()->WaitIdle();
    VerifyPublishedColor(expected, policy->PublishedFrame(), policy->SceneColor(next.Slot));
    const auto frames = policy->SubmittedFrames();
    Require(policy->UploadedWorldBytes() > 0 && policy->UploadedWorldBytes() <= frames * Grid2D::UploadBytes, "regional upload accounting incorrect");
    const auto generation = engine.GetRHI()->GetSwapchainGeneration();
    const auto cameraBeforeResize = input->GetCamera().GetPosition();
    glfwSetWindowSize(engine.GetPlatform()->GetWindows(), 640, 480);
    RHIFrameToken token{};
    for (int attempt = 0; attempt < 100; ++attempt) {
        token = Draw(app);
        if (engine.GetRHI()->GetSwapchainGeneration() > generation)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(engine.GetRHI()->GetSwapchainGeneration() > generation, "resize did not recreate the swapchain");
    Require(input->GetCamera().GetPosition() == cameraBeforeResize, "resize changed camera world centre");
    int width = 0, height = 0;
    engine.GetPlatform()->GetFramebufferSize(width, height);
    Require(policy->SceneColor(token.Slot).Desc.Width == uint32_t(width) && policy->SceneColor(token.Slot).Desc.Height == uint32_t(height), "resize color extent stale");
    engine.GetRHI()->WaitIdle();
    expected.View = input->GetView();
    VerifyPublishedColor(expected, policy->PublishedFrame(), policy->SceneColor(token.Slot));

    engine.GetPlatform()->OnKeyButtonCallback(GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
    engine.GetInput()->Dispatch();
    Require(input->GetPresentation().Post.Values[1] == 0.35f, "input without ImGui failed");
    engine.GetPlatform()->OnKeyButtonCallback(GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
    engine.GetInput()->Dispatch();
    const auto beforeMinimize = policy->SubmittedFrames();
    const auto beforeSkipPublications = publications;
    const auto beforeSkipStorage = CopyWorldCells(pixel::testing::SessionAccess::World(input->GetSession()));
    glfwIconifyWindow(engine.GetPlatform()->GetWindows());
    for (int attempt = 0; attempt < 50 && !engine.GetPlatform()->IsWindowMinimized(); ++attempt) {
        engine.GetPlatform()->PollEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (engine.GetPlatform()->IsWindowMinimized()) {
        Require(!app.FrameBegin(), "minimized frame should be skipped");
        Require(policy->SubmittedFrames() == beforeMinimize && publications == beforeSkipPublications && CopyWorldCells(pixel::testing::SessionAccess::World(input->GetSession())) == beforeSkipStorage, "minimized frame published/uploaded/submitted");
    } else {
        std::cout << "Minimize check skipped: window manager did not report iconification\n";
    }
    pixel::testing::SessionAccess::World(input->GetSession()).Fill({static_cast<pixel::CellTypeId>(20)});
    std::fill(expectedCells.begin(), expectedCells.end(), 2);
    expected.View = input->GetView();
    expected.Post = input->GetPresentation().Post;
    glfwRestoreWindow(engine.GetPlatform()->GetWindows());
    token = Draw(app);
    engine.GetRHI()->WaitIdle();
    VerifyPublishedColor(expected, policy->PublishedFrame(), policy->SceneColor(token.Slot));
    CheckPublishedRegion(policy->PublishedFrame(), expectedCells);
    const auto beforeEnd = publications;
    app.Shutdown();
    Require(publications == beforeEnd, "shutdown invoked gameplay publisher");

    std::cout << "Pixel2D GPU lifecycle " << iteration << " passed: grid pages, full textures, alpha, zoom/crop, post tint, resize/minimize, no-UI input\n";
}
void VerifyDefaultWorldField(const pixel::World &world) {
    const auto state = world.ReadState();
    Require(world.GetWindStrategyKind() == pixel::WindStrategyKind::Empty, "default world must use empty wind");
    size_t stableAirCells = 0;
    for (uint32_t y = 0; y < state.Height; ++y)
        for (uint32_t x = 0; x < state.Width; ++x) {
            const auto cell = state.AtLocal(x, y);
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
    Require(world.SampleWind(512.5f, 423.5f) == pixel::WindVelocity{}, "default empty wind is not calm");
    Require(stableAirCells == size_t(600 - 512) * state.Width, "default world does not have 88 stable sky rows");
}
std::optional<CharacterDraw> ExpectedCharacter(const PixelGame &game) {
    if (!game.GetSession().GetPlayer() || game.GetPresentation().DisplayMode != GridDisplayMode::Materials)
        return std::nullopt;
    const auto &player = *game.GetSession().GetPlayer();
    int width, height;
    GAxiom->Engine->GetPlatform()->GetFramebufferSize(width, height);
    const auto centre = game.GetCamera().GetPosition();
    const auto scale = game.GetView().CellSizePixels;
    CharacterDraw expected;
    expected.Bounds = {float((player.X - centre[0]) * scale[0] + width / 2.0),
                       float((player.Y - centre[1]) * scale[1] + height / 2.0), float(2 * scale[0]), float(4 * scale[1])};
    expected.HeadColor = {Linear(255), Linear(212), Linear(140), 1};
    expected.BodyColor = {Linear(40), Linear(210), Linear(180), 1};
    return expected;
}
void FormulaGameplayGpu() {
    auto game = std::make_unique<PixelGame>(false);
    auto *input = game.get();
    input->GetPresentation().WindVisual.Mode = WindVisualMode::Off;
    PixelRender *policy = nullptr;
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi, IRenderSceneInterface *) {
        auto result = std::make_unique<PixelRender>(rhi, pixel::GamePresentation::MakeTextures(), input->CreateFramePublisher());
        policy = result.get();
        return result;
    };
    PixelSmokeApplication app(std::move(game));
    app.Engine->SetRender(std::move(setup));
    app.Initialize();
    app.Begin();
    auto &runtime = input->GetSession().GetFormulaRuntime();
    Require(bool(input->GetSession().ApplyProgram(pixel::NativeCaller::Driver, pixel::formula::Formula::Bolt().Spread(3, 12))), "GPU formula fixture rejected a fan");
    Require(input->GetSession().Execute(pixel::NativeCaller::Driver, pixel::api::ActivateProgram{input->GetSession().AppliedProgram(), {{600, 510}, {0, 0}}}).Applied(), "GPU fixture failed to launch");
    const auto verifyYellow = [&] {
        const auto token = Draw(app);
        app.Engine->GetRHI()->WaitIdle();
        const auto frame = policy->PublishedFrame();
        Require(frame.FormulaDraws.size() == 3, "GPU publication lost fan projectiles");
        const auto &scene = policy->SceneColor(token.Slot);
        const auto pixels = ReadColor(scene);
        for (const auto &draw : frame.FormulaDraws) {
            const auto x = uint32_t(draw.CenterRadius[0]), y = uint32_t(draw.CenterRadius[1]);
            Require(x < scene.Desc.Width && y < scene.Desc.Height, "GPU sphere fixture is outside the viewport");
            const auto offset = (size_t(y) * scene.Desc.Width + x) * 4;
            Require(pixels[offset] == 255 && pixels[offset + 1] == 255 && pixels[offset + 2] == 0 && pixels[offset + 3] == 255,
                    "sphere centre was not opaque yellow after composition");
        }
    };
    for (int slot = 0; slot < 4; ++slot)
        verifyYellow();
    input->GetSession().Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    input->Tick(1.f / 60);
    verifyYellow();
    input->GetPresentation().ToggleFieldView();
    Draw(app);
    Require(policy->PublishedFrame().FormulaDraws.empty() && runtime.Projectiles().size() == 3,
            "F1 must hide formula rendering without removing instances");
    input->GetPresentation().ToggleFieldView();
    verifyYellow();
    auto *platform = app.Engine->GetPlatform();
    const auto generation = app.Engine->GetRHI()->GetSwapchainGeneration();
    glfwSetWindowSize(platform->GetWindows(), 720, 540);
    for (int attempt = 0; attempt < 100 && app.Engine->GetRHI()->GetSwapchainGeneration() == generation; ++attempt) {
        Draw(app);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Require(app.Engine->GetRHI()->GetSwapchainGeneration() > generation, "formula resize fixture never recreated targets");
    verifyYellow();
    Require(input->GetSession().Control(pixel::NativeCaller::Driver, pixel::api::ClearRunning{}).Succeeded(), "GPU formula clear failed");
    Draw(app);
    Require(policy->PublishedFrame().FormulaDraws.empty(), "cleared GPU formula snapshot retained a sphere");
    auto &session = input->GetSession();
    Require(bool(session.ApplyProcess(pixel::NativeCaller::Driver, pixel::CarrierExample())), "process GPU apply failed");
    Require(session.Execute(pixel::NativeCaller::Input, pixel::api::ActivateProgram{session.AppliedProgram(), {{600, 500}, {0, 0}}}).Applied(),
            "process GPU player activation failed");
    for (const auto kind : {pixel::ProcessCarrierKind::NaturalSource, pixel::ProcessCarrierKind::FixedFixture}) {
        pixel::ProcessCarrier carrier;
        carrier.Kind = kind;
        carrier.Origin = pixel::WorldPosition::FromAbsolute(kind == pixel::ProcessCarrierKind::NaturalSource ? 518.5 : 520.5, 506.5);
        carrier.Scope = session.GetWorld().GetSimulationBounds();
        Require(session.StartProcessFixture(pixel::NativeCaller::Driver, carrier).Succeeded(), "process GPU fixture failed");
    }
    verifyYellow();
    session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    input->Tick(1.f / 60);
    verifyYellow();
    input->GetPresentation().ToggleFieldView();
    Draw(app);
    Require(policy->PublishedFrame().FormulaDraws.empty() && runtime.Projectiles().size() == 3, "F1 lost process instances");
    input->GetPresentation().ToggleFieldView();
    session.Control(pixel::NativeCaller::Driver, pixel::api::ClearRunning{});
    Draw(app);
    Require(policy->PublishedFrame().FormulaDraws.empty(), "process GPU clear retained a sphere");
    auto &inventory = pixel::testing::SessionAccess::Inventory(session);
    const std::array cost{pixel::items::ItemAmount{pixel::items::ItemId::Stone, 4},
                          pixel::items::ItemAmount{pixel::items::ItemId::CopperOre, 2},
                          pixel::items::ItemAmount{pixel::items::ItemId::Coal, 1}};
    Require(inventory.Add(inventory.Reference(session.PlayerInventory()), cost) == pixel::items::Result::Applied,
            "device GPU fixture stock failed");
    Require(session.Execute(pixel::NativeCaller::Input, pixel::api::PlaceDevice{{520, 508}}).Applied(), "device GPU placement failed");
    const auto deviceId = session.GetDevices().Snapshot().front().Ref.Id;
    const auto currentDevice = [&] { return session.GetDevices().Find(deviceId)->Value; };
    const auto verifyDevice = [&] {
        const auto token = Draw(app);
        app.Engine->GetRHI()->WaitIdle();
        const auto frame = policy->PublishedFrame();
        Require(frame.DeviceDraws.size() == 1, "GPU publication lost device");
        const auto &draw = frame.DeviceDraws.front();
        const auto &scene = policy->SceneColor(token.Slot);
        const auto pixels = ReadColor(scene);
        const auto check = [&](const auto &bounds, const auto &color) {
            const auto x = uint32_t(bounds[0]), y = uint32_t(bounds[1]);
            Require(x < scene.Desc.Width && y < scene.Desc.Height, "GPU device fixture outside viewport");
            for (size_t c = 0; c < 4; ++c)
                Require(std::abs(int(pixels[(size_t(y) * scene.Desc.Width + x) * 4 + c]) - int(std::lround(color[c] * 255))) <= 1,
                        "GPU device body or output color mismatch");
        };
        check(draw.CenterRadius, draw.Color);
        check(draw.OutputCenterRadius, draw.OutputColor);
    };
    for (int slot = 0; slot < 4; ++slot) verifyDevice();
    Require(session.Execute(pixel::NativeCaller::Input, pixel::api::SupplyDevice{currentDevice().Ref}).Applied(), "GPU fuel failed");
    Require(session.Execute(pixel::NativeCaller::Driver, pixel::api::SetWindBoundary{600}).Applied() &&
            session.Execute(pixel::NativeCaller::Driver, pixel::api::SelectWindStrategy{pixel::api::WindKind::Coarse}).Applied() &&
            session.Execute(pixel::NativeCaller::Input, pixel::api::SetDeviceEnabled{currentDevice().Ref, true}).Applied(),
            "GPU device enable failed");
    session.BeginFrame();
    session.Control(pixel::NativeCaller::Driver, pixel::api::StepSimulation{});
    input->Tick(1.f / 60);
    Require(currentDevice().State == pixel::DeviceState::Running, "GPU device did not start");
    verifyDevice();
    input->GetPresentation().ToggleFieldView();
    Draw(app);
    Require(policy->PublishedFrame().DeviceDraws.empty() && session.GetDevices().Count() == 1, "F1 removed device state");
    input->GetPresentation().ToggleFieldView();
    Require(session.Execute(pixel::NativeCaller::Input, pixel::api::RemoveDevice{currentDevice().Ref}).Applied(), "GPU device removal failed");
    Draw(app);
    Require(policy->PublishedFrame().DeviceDraws.empty() && session.GetFormulaRuntime().Stats().Roots == 0,
            "GPU removal retained device or runtime");
    app.Shutdown();
    std::cout << "Formula GPU passed: legacy fan and process three carriers, yellow readback, slots, step, F1, resize, device body/port readback and clear\n";
}

void BasicWorldGameplay() {
    auto game = std::make_unique<PixelGame>(false);
    auto *input = game.get();
    input->GetPresentation().WindVisual.Mode = WindVisualMode::Off;
    PixelRender *policy = nullptr;
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi, IRenderSceneInterface *) {
        auto result = std::make_unique<PixelRender>(rhi, pixel::GamePresentation::MakeTextures(), input->CreateFramePublisher());
        policy = result.get();
        return result;
    };
    PixelSmokeApplication app(std::move(game));
    app.Engine->SetRender(std::move(setup));
    auto &engine = *app.Engine;
    app.Initialize();
    app.Begin();
    auto &world = pixel::testing::SessionAccess::World(input->GetSession());
    VerifyDefaultWorldField(world);
    const auto supplySoil = [&] {
        auto &session = input->GetSession();
        auto &inventory = pixel::testing::SessionAccess::Inventory(session);
        const std::array stock{pixel::items::ItemAmount{pixel::items::ItemId::Soil, 64}};
        Require(inventory.Add(inventory.Reference(session.PlayerInventory()), stock) == pixel::items::Result::Applied,
                "GPU click fixture could not supply soil");
    };
    supplySoil();
    Require(input->GetSession().IsPaused(), "application must preserve the default paused session");
    const auto initialWind = world.GetWind().Sample(512.5f, 200.5f);
    std::vector<uint32_t> expectedCells(Grid2D::CellCount);
    std::fill(expectedCells.begin() + 512 * Grid2D::Width, expectedCells.end(), 1);
    size_t gameplayFrame = 0;
    auto verify = [&] {
        ++gameplayFrame;
        const auto token = Draw(app);
        engine.GetRHI()->WaitIdle();
        CheckPublishedRegion(policy->PublishedFrame(), expectedCells);
        VerifyPublishedColor({expectedCells, input->GetView(), input->GetPresentation().Post}, policy->PublishedFrame(), policy->WorldColor(token.Slot), pixel::GamePresentation::MakeTextures());
        VerifyPublishedColor({expectedCells, input->GetView(), input->GetPresentation().Post, GridDisplayMode::Materials, ExpectedCharacter(*input)}, policy->PublishedFrame(),
                             policy->SceneColor(token.Slot), pixel::GamePresentation::MakeTextures());
    };
    verify(); // GPU readback includes the two solid-colour character rectangles.
    // Reproduce a queued click sampled before a resize: FrameBegin has already
    // refreshed the current viewport, but this event retains its own dimensions.
    int currentWidth, currentHeight;
    engine.GetPlatform()->GetFramebufferSize(currentWidth, currentHeight);
    for (int delta : {-320, 320}) {
        MousePointerState pointer;
        pointer.FramebufferSize = {currentWidth + delta, currentHeight + delta};
        pointer.WindowSize = {pointer.FramebufferSize[0] / 2, pointer.FramebufferSize[1] / 2};
        const auto centre = input->GetCamera().GetPosition();
        const auto scale = input->GetView().CellSizePixels;
        const std::array<double, 2> target{512.5, 507.5};
        for (size_t axis = 0; axis < 2; ++axis) {
            const double oldOrigin = std::round(centre[axis] * scale[axis] - pointer.FramebufferSize[axis] / 2.0);
            pointer.Position[axis] = (target[axis] * scale[axis] - oldOrigin) * pointer.WindowSize[axis] / pointer.FramebufferSize[axis];
        }
        input->OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_LEFT, ButtonAction::ACTION_PRESS, 0, pointer);
        Require(world.GetCell(512, 507).Type == pixel::CellTypes::Dirt,
                "queued click used current viewport instead of event viewport after resize");
        input->OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_RIGHT, ButtonAction::ACTION_PRESS, int(Mod::MOD_SHIFT), pointer);
        Require(world.GetCell(512, 507).Type == pixel::CellTypes::Empty, "queued removal after resize missed original cell");
        pointer.FramebufferSize = {-1, 0};
        input->OnMouseButtonEvent(MouseButton::MOUSE_BUTTON_LEFT, ButtonAction::ACTION_PRESS, 0, pointer);
    }
    verify(); // No accidental edits at the displaced coordinates.

    const auto initialCamera = input->GetCamera().GetPosition();
    for (int quarterPixel = 1; quarterPixel <= 4; ++quarterPixel) {
        pixel::testing::SessionAccess::SetPlayerPosition(input->GetSession(), pixel::WorldPosition::FromAbsolute(511 + quarterPixel / 64.f, 508 + quarterPixel / 64.f));
        verify(); // Each quarter-pixel shift must produce the expected edge coverage.
        const auto sprite = policy->PublishedFrame().Character;
        Require(sprite && std::abs(sprite->Bounds[0] - ExpectedCharacter(*input)->Bounds[0]) < 1e-6,
                "subpixel position was quantized during publication");
        Require(input->GetCamera().GetPosition() == initialCamera, "render-only fixture changed camera");
    }
    const auto unscaledView = input->GetView();
    for (uint32_t scale : {1u, 8u, 32u}) {
        auto clipped = unscaledView;
        clipped.CellSizePixels = {scale, scale};
        clipped.OriginPixels = {int32_t(511 * scale + 1), int32_t(508 * scale + 1)};
        clipped.WorldOriginCells = {};
        input->SetView(clipped);
        pixel::testing::SessionAccess::SetPlayerPosition(input->GetSession(), pixel::WorldPosition::FromAbsolute(511.0625f, 508.0625f));
        verify(); // Fractional rectangle clipped by the viewport, including minimum zoom.
    }
    input->SetView(unscaledView);
    pixel::testing::SessionAccess::SetPlayerPosition(input->GetSession(), pixel::WorldPosition::FromAbsolute(511, 508));

    Require(input->GetControlMode() == PixelGame::ControlMode::Standard, "default mode must control the character");
    input->GetSession().Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
    const auto playerX = input->GetSession().GetPlayer()->X;
    const auto camera = input->GetView().OriginPixels;
    input->OnKeyEvent(Key::KEY_D, 0, ButtonAction::ACTION_PRESS, Mod{});
    input->Tick(0.125f);
    Require(input->GetSession().GetPlayer()->X > playerX && input->GetView().OriginPixels[0] > camera[0], "standard mode must move and follow player");
    const double targetX = input->GetSession().GetPlayer()->X + pixel::Character::Width / 2;
    Require(input->GetCamera().GetPosition()[0] < targetX, "camera must approach player gradually instead of snapping");
    Require(std::abs(input->GetCamera().GetPosition()[0] - (playerX + 1 + (targetX - playerX - 1) * (1 - std::exp(-1.0)))) < 1e-6,
            "camera must follow distance-proportional speed");
    input->OnKeyEvent(Key::KEY_F3, 0, ButtonAction::ACTION_PRESS, Mod{});
    const auto debugX = input->GetSession().GetPlayer()->X;
    const auto debugCamera = input->GetView().OriginPixels;
    engine.GetPlatform()->OnKeyButtonCallback(GLFW_KEY_D, 0, GLFW_REPEAT, 0);
    engine.GetInput()->Dispatch();
    input->Tick(0.125f);
    Require(input->GetSession().GetPlayer()->X == debugX && input->GetView().OriginPixels == debugCamera, "switch must clear held input");
    input->OnKeyEvent(Key::KEY_D, 0, ButtonAction::ACTION_PRESS, Mod{});
    input->Tick(0.125f);
    Require(input->GetSession().GetPlayer()->X == debugX && input->GetView().OriginPixels[0] > debugCamera[0], "debug mode must move camera only");
    input->OnKeyEvent(Key::KEY_F3, 0, ButtonAction::ACTION_REPEAT, Mod{});
    Require(input->GetControlMode() == PixelGame::ControlMode::DebugCamera, "F3 repeat toggled mode");
    const auto beforeReturn = input->GetCamera().GetPosition();
    input->OnKeyEvent(Key::KEY_F3, 0, ButtonAction::ACTION_PRESS, Mod{});
    Require(input->GetCamera().GetPosition() == beforeReturn, "return to standard must not snap camera");
    input->Tick(0.125f);
    Require(std::abs(input->GetCamera().GetPosition()[0] -
                     (beforeReturn[0] + (input->GetSession().GetPlayer()->X + 1 - beforeReturn[0]) * (1 - std::exp(-1.0)))) < 1e-6,
            "return from debug must smoothly approach player from current camera position");
    engine.GetPlatform()->OnKeyButtonCallback(GLFW_KEY_D, 0, GLFW_REPEAT, 0);
    engine.GetInput()->Dispatch();
    input->Tick(0.125f);
    Require(input->GetSession().GetPlayer()->X == debugX, "return to standard retained movement after repeat");
    // UI capture and focus loss use this same clear hook. A held key must not
    // become a new press when repeat events resume after the clear.
    for (const auto mode : {PixelGame::ControlMode::Standard, PixelGame::ControlMode::DebugCamera}) {
        input->SetControlMode(mode);
        for (const int key : {GLFW_KEY_W, GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D}) {
            engine.GetPlatform()->OnKeyButtonCallback(key, 0, GLFW_PRESS, 0);
            engine.GetInput()->Dispatch();
            input->ClearInputState();
            const auto clearedX = input->GetSession().GetPlayer()->X;
            const auto clearedCamera = input->GetView().OriginPixels;
            engine.GetPlatform()->OnKeyButtonCallback(key, 0, GLFW_REPEAT, 0);
            engine.GetInput()->Dispatch();
            input->Tick(0.125f);
            Require(input->GetSession().GetPlayer()->X == clearedX && (mode == PixelGame::ControlMode::Standard || input->GetView().OriginPixels == clearedCamera),
                    "repeat reactivated movement after input clear");
            engine.GetPlatform()->OnKeyButtonCallback(key, 0, GLFW_RELEASE, 0);
            engine.GetInput()->Dispatch();
        }
        const auto beforePressX = input->GetSession().GetPlayer()->X;
        const auto beforePressCamera = input->GetView().OriginPixels;
        engine.GetPlatform()->OnKeyButtonCallback(GLFW_KEY_D, 0, GLFW_PRESS, 0);
        engine.GetPlatform()->OnKeyButtonCallback(GLFW_KEY_D, 0, GLFW_REPEAT, 0);
        engine.GetInput()->Dispatch();
        input->Tick(0.125f);
        Require(mode == PixelGame::ControlMode::Standard ? input->GetSession().GetPlayer()->X > beforePressX
                                                         : input->GetView().OriginPixels[0] > beforePressCamera[0],
                "fresh press followed by repeat must preserve movement");
        engine.GetPlatform()->OnKeyButtonCallback(GLFW_KEY_D, 0, GLFW_RELEASE, 0);
        engine.GetInput()->Dispatch();
        const auto releasedX = input->GetSession().GetPlayer()->X;
        const auto releasedCamera = input->GetView().OriginPixels;
        input->Tick(0.125f);
        Require(input->GetSession().GetPlayer()->X == releasedX && (mode == PixelGame::ControlMode::Standard || input->GetView().OriginPixels == releasedCamera),
                "release must stop movement");
    }
    input->SetControlMode(PixelGame::ControlMode::Standard);
    input->OnKeyEvent(Key::KEY_SPACE, 0, ButtonAction::ACTION_PRESS, Mod{});
    input->Tick(1.f / 60);
    Require(input->GetSession().GetPlayer()->Y < 508, "standard Space must jump");
    verify();
    input->SetControlMode(PixelGame::ControlMode::DebugCamera);
    pixel::testing::SessionAccess::SetPlayerPosition(input->GetSession(), pixel::WorldPosition::FromAbsolute(530, 508)); // Leave the legacy editing fixtures unobstructed.
    // Exercise the real default field: empty sky must not disturb the terrain.
    for (int i = 0; i < 5; ++i)
        input->Tick(1.f / 60);
    verify();
    // Explicitly enable coarse wind for the dynamic-wind GPU fixture.
    world.ConfigureWind(424);
    const auto originalView = input->GetView();
    {
        auto view = input->GetView();
        view.OriginPixels = {500 * 16, 190 * 16};
        view.WorldOriginCells = {};
        input->SetView(view);
    }
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
    {
        auto view = input->GetView();
        view = originalView;
        input->SetView(view);
    }
    auto *platform = engine.GetPlatform();
    auto moveCursorToCell = [&](uint32_t x, uint32_t y) {
        int ww = 0, wh = 0, fw = 0, fh = 0;
        glfwGetWindowSize(platform->GetWindows(), &ww, &wh);
        platform->GetFramebufferSize(fw, fh);
        const double px = (x + .5) * input->GetView().CellSizePixels[0] - input->GetView().OriginPixels[0];
        const double py = (y + .5) * input->GetView().CellSizePixels[1] - input->GetView().OriginPixels[1];
        Require(px >= 0 && py >= 0 && px < fw && py < fh, "test clicked a non-visible cell");
        glfwSetCursorPos(platform->GetWindows(), px * ww / fw, py * wh / fh);
    };
    auto queueClick = [&](uint32_t x, uint32_t y, int button) {
        moveCursorToCell(x, y);
        platform->OnButtonCallback(button, GLFW_PRESS, button == GLFW_MOUSE_BUTTON_RIGHT ? GLFW_MOD_SHIFT : 0);
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
    const auto cameraBeforeZoom = input->GetCamera().GetPosition();
    input->OnKeyEvent(Key::KEY_EQUAL, 0, ButtonAction::ACTION_PRESS, Mod{});
    Require(input->GetCamera().GetPosition() == cameraBeforeZoom, "zoom changed camera world centre");
    {
        auto view = input->GetView();
        view.OriginPixels[0] += 16;
        input->SetView(view);
    }
    {
        auto view = input->GetView();
        view.OriginPixels[1] -= 16;
        input->SetView(view);
    }
    clickCell(514, 510, GLFW_MOUSE_BUTTON_LEFT);
    expectedCells[510 * Grid2D::Width + 514] = 1;
    verify();
    clickCell(514, 510, GLFW_MOUSE_BUTTON_RIGHT);
    expectedCells[510 * Grid2D::Width + 514] = 0;
    verify();
    world.FillForceDirection({0, 0});
    world.AddWindImpulse(512, 200, 64, {0, -120});
    input->OnKeyEvent(Key::KEY_R, 0, ButtonAction::ACTION_PRESS, Mod{});
    Require(input->GetSession().IsPaused(), "reset must pause the session");
    supplySoil();
    input->GetSession().Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
    VerifyDefaultWorldField(world);
    Require(input->GetSession().GetPlayer() && input->GetSession().GetPlayer()->X == 511 && input->GetSession().GetPlayer()->Y == 508, "R must reset player");
    pixel::testing::SessionAccess::SetPlayerPosition(input->GetSession(), pixel::WorldPosition::FromAbsolute(530, 508));
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
    app.Shutdown();

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
    auto game = std::make_unique<PixelGame>(false);
    auto *input = game.get();
    input->GetPresentation().WindVisual.Mode = WindVisualMode::Off;
    PixelRender *policy = nullptr;
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi, IRenderSceneInterface *) {
        auto result = std::make_unique<PixelRender>(rhi, pixel::GamePresentation::MakeTextures(), input->CreateFramePublisher());
        policy = result.get();
        return result;
    };
    PixelSmokeApplication app(std::move(game));
    app.Engine->SetRender(std::move(setup));
    auto &engine = *app.Engine;
    app.Initialize();
    app.Begin();
    auto &world = pixel::testing::SessionAccess::World(input->GetSession());
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
    const auto initial = CopyWorldCells(world);
    auto press = [&](Key key) { input->OnKeyEvent(key, 0, ButtonAction::ACTION_PRESS, Mod{}); };
    input->OnKeyEvent(Key::KEY_F1, 0, ButtonAction::ACTION_REPEAT, Mod{});
    input->OnKeyEvent(Key::KEY_F1, 0, ButtonAction::ACTION_RELEASE, Mod{});
    Require(input->GetPresentation().DisplayMode == GridDisplayMode::Materials, "F1 repeat/release changed the display mode");
    press(Key::KEY_T);
    press(Key::KEY_F1);
    size_t debugFrame = 0;
    auto verifyField = [&](int tolerance = 0) {
        ++debugFrame;
        const auto token = Draw(app);
        engine.GetRHI()->WaitIdle();
        const auto frame = policy->PublishedFrame();
        Require(frame.DisplayMode == GridDisplayMode::ForceField && frame.Post.Values == PostProcessParameters{}.Values, "debug frame mode/tint is wrong");
        CheckPublishedRegion(frame, expected, tolerance);
        VerifyPublishedColor({expected, input->GetView(), {}, GridDisplayMode::ForceField}, policy->PublishedFrame(), policy->SceneColor(token.Slot));
        return token;
    };
    for (int page = 0; page < int(Grid2D::PageCount); ++page) {
        {
            auto view = input->GetView();
            view.OriginPixels = {-8, (page * int(Grid2D::PageHeight) - 2) * 16};
            view.WorldOriginCells = {};
            input->SetView(view);
        }
        verifyField(); // Includes page seams, outside-world pixels, air and dirt.
    }
    press(Key::KEY_MINUS);
    verifyField();
    Require(initial == CopyWorldCells(world), "debug view changed simulation state");
    press(Key::KEY_F1);
    const auto materialToken = Draw(app);
    engine.GetRHI()->WaitIdle();
    Require(policy->PublishedFrame().DisplayMode == GridDisplayMode::Materials && policy->PublishedFrame().Post.Values == input->GetPresentation().Post.Values
                && input->GetPresentation().Post.Values[1] == 0.35f,
            "leaving debug view did not restore material mode and tint");
    for (size_t i = 0; i < expected.size(); ++i)
        expected[i] = initial[i].Type == pixel::CellTypes::Dirt ? 1 : 0;
    VerifyPublishedColor({expected, input->GetView(), input->GetPresentation().Post, GridDisplayMode::Materials, ExpectedCharacter(*input)}, policy->PublishedFrame(), policy->SceneColor(materialToken.Slot), pixel::GamePresentation::MakeTextures());

    press(Key::KEY_F1);
    press(Key::KEY_R);
    world.ConfigureWind(424);
    input->GetSession().Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
    Require(input->GetPresentation().DisplayMode == GridDisplayMode::ForceField, "world reset unexpectedly disabled debug view");
    {
        auto view = input->GetView();
        view.CellSizePixels = {1, 1};
        input->SetView(view);
    }
    {
        auto view = input->GetView();
        view.OriginPixels = {0, 0};
        view.WorldOriginCells = {};
        input->SetView(view);
    }
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
    world.ConfigureWind(424);
    input->GetSession().Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
    pixel::testing::SimulatedWorld clearSky(Grid2D::Width, Grid2D::Height);
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
    {
        auto view = input->GetView();
        view.OriginPixels = {448 * 8, 144 * 8};
        view.WorldOriginCells = {};
        input->SetView(view);
    }
    {
        auto view = input->GetView();
        view.CellSizePixels = {8, 8};
        input->SetView(view);
    }
    verifyField(1);
    press(Key::KEY_R);
    world.ConfigureWind(424);
    input->GetSession().Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
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
    // Open boundaries move the deflection peak relative to the old periodic
    // fixture. Preserve its angle requirement over the stack's local airflow,
    // then verify that the GPU actually displays the strongest CPU deflection.
    double largestAngle = 0;
    std::array<int64_t, 2> peak{};
    for (int64_t y = 312; y < 424; ++y) {
        for (int64_t x = 448; x < 576; ++x) {
            const auto coupled = world.GetFieldVelocity(x, y);
            const auto reference = clearSky.GetFieldVelocity(x, y);
            const double angle = std::abs(std::atan2(double(coupled.X) * reference.Y - double(coupled.Y) * reference.X,
                                                     double(coupled.X) * reference.X + double(coupled.Y) * reference.Y));
            if (angle > largestAngle) {
                largestAngle = angle;
                peak = {x, y};
            }
        }
    }
    Require(largestAngle > 0.1, "narrow dirt stack did not visibly deflect the default combined field");
    expected = ExpectedFieldColors(world);
    {
        auto view = input->GetView();
        view.OriginPixels = {448 * 8, 312 * 8};
        view.WorldOriginCells = {};
        view.CellSizePixels = {8, 8};
        input->SetView(view);
    }
    const auto narrowFrame = verifyField(1);
    const auto pixels = ReadColor(policy->WorldColor(narrowFrame.Slot));
    const auto published = policy->PublishedFrame();
    const auto framebuffer = policy->WorldColor(narrowFrame.Slot).Desc;
    std::array<int64_t, 2> pixel{};
    for (size_t axis = 0; axis < 2; ++axis)
        pixel[axis] = (peak[axis] - published.View.WorldOriginCells[axis]) * published.View.CellSizePixels[axis] - published.View.OriginPixels[axis] + published.View.CellSizePixels[axis] / 2;
    Require(pixel[0] >= 0 && pixel[1] >= 0 && pixel[0] < framebuffer.Width && pixel[1] < framebuffer.Height,
            "narrow-stack deflection peak was omitted from the GPU fixture");
    const auto expectedPeak = expected[size_t(peak[1]) * Grid2D::Width + size_t(peak[0])];
    for (size_t channel = 0; channel < 3; ++channel) {
        const int actual = pixels[(size_t(pixel[1]) * framebuffer.Width + size_t(pixel[0])) * 4 + channel];
        Require(std::abs(actual - int((expectedPeak >> (channel * 8)) & 255)) <= 2,
                "GPU F1 colour disagrees with the CPU narrow-stack deflection peak");
    }
    press(Key::KEY_R);
    world.ConfigureWind(424);
    input->GetSession().Control(pixel::NativeCaller::Driver, pixel::api::SetPaused{false});
    clearSky.GenerateTerrain(512);
    clearSky.ConfigureWind(424);
    expected = ExpectedFieldColors(clearSky);
    verifyField(1);
    app.Shutdown();

    std::cout << "Force-field debug GPU view passed: signed/zero directions, pages, F1/tint, reset, dynamic wind, dirt patch and narrow stack\n";
}

void TerrainMaterialsGpu() {
    pixel::ArtAssetStore store(AssetLoader::LoadContentFile);
    pixel::GamePresentation presentation;
    const auto catalog = presentation.LoadTextures(store);
    pixel::testing::SimulatedWorld world(64, 64);
    constexpr std::array types{pixel::CellTypeId::Dirt, pixel::CellTypeId::GroundSoil,
        pixel::CellTypeId::GrassSoil, pixel::CellTypeId::Stone, pixel::CellTypeId::CoalOre,
        pixel::CellTypeId::CopperOre, pixel::CellTypeId::IronOre, pixel::CellTypeId::TungstenOre,
        pixel::CellTypeId::GoldOre, pixel::CellTypeId::Water};
    for (uint32_t i = 0; i < types.size(); ++i)
        world.FillRegion({0, 2 + i * 4, 64, 3}, {types[i]});
    std::array<int64_t, 2> origin{};
    PixelRender *policy = nullptr;
    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi, IRenderSceneInterface *) {
        auto render = std::make_unique<PixelRender>(rhi, catalog, [&](WorldGridFrame &frame) {
            BuildWorldGridFrame(world.ReadState(), presentation.Appearances, frame);
            const std::vector<uint32_t> cells(frame.Cells().begin(), frame.Cells().end());
            frame.SetRegion(origin, 64, 64);
            std::copy(cells.begin(), cells.end(), frame.Cells().begin());
            frame.View.WorldOriginCells = origin;
            frame.View.CellSizePixels = {8, 8};
            frame.View.Background = {.15f, .25f, .35f, 1};
        });
        policy = render.get();
        return render;
    };
    PixelSmokeApplication app(std::make_unique<PixelGame>(false));
    app.Engine->SetRender(std::move(setup));
    app.Initialize(); app.Begin();
    for (int i = 0; i < 6; ++i) {
        if (i == 3) origin = {(int64_t{1} << 55) + 19, -(int64_t{1} << 55) - 7};
        const auto token = Draw(app);
        app.Engine->GetRHI()->WaitIdle();
        VerifyColor(policy->PublishedFrame(), policy->WorldColor(token.Slot), catalog);
    }
    app.Shutdown();
    std::cout << "Terrain GPU passed: Content PNGs, all materials, water alpha/surface strips, frame slots and distant UV phases\n";
}

void ArtBackgroundGpu(bool exactMirrorEdge = false) {
    pixel::ArtAssetStore store(AssetLoader::LoadContentFile);
    auto assets = std::make_shared<pixel::BackgroundAssets>(*store.LoadBackground("ArtAssets.json"));
    GridView2D view;
    view.Background = {.25f, .55f, .85f, 1};
    bool field = false, terrain = false;
    PixelRender *policy = nullptr;

    RenderSetup setup;
    setup.Enable3DScene = false;
    setup.CreatePolicy = [&](DynamicRHI &rhi, IRenderSceneInterface *) {
        auto render = std::make_unique<PixelRender>(rhi, pixel::GamePresentation::MakeTextures(), [&](WorldGridFrame &frame) {
            frame.SetRegion({0, 0}, 128, 128);
            frame.View = view;
            frame.DisplayMode = field ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
            const auto extent = GAxiom->Engine->GetRHI()->GetPresentationExtent();
            const auto ground = uint32_t(extent.Height * .56f) / view.CellSizePixels[1];
            for (uint32_t y = 0; y < 128; ++y)
                for (uint32_t x = 0; x < 128; ++x)
                    frame.Cells()[size_t(y) * 128 + x] = field ? 0x8080FF : (terrain && y >= ground ? 1 : 0);
        }, "Pixel2DPostProcess.fs", assets);
        policy = render.get();
        return render;
    };

    auto game = std::make_unique<PixelGame>(false);
    auto *gamePointer = game.get();
    const auto cached = game->GetArtAssets().LoadBackground("ArtAssets.json");
    PixelSmokeApplication app(std::move(game));
    Require(gamePointer->GetArtAssets().LoadBackground("ArtAssets.json") == cached, "temporary render setup discarded the game asset cache");
    app.Engine->SetRender(std::move(setup));
    app.Initialize(); app.Begin();
    auto &rhi = *app.Engine->GetRHI();

    if (exactMirrorEdge) {
        const auto extent = rhi.GetPresentationExtent();
        // Change only test-owned presentation settings before first publication.
        // The RHI extent is valid now; the factory runs before window creation.
        const uint32_t edgeX = (extent.Width * 3 / 4 / 7) * 7 + 3;
        const float displayedWidth = 2.f * (edgeX - extent.Width / 2) + 1;
        assets->HeightFraction = displayedWidth * assets->Layers[0].Image->Height / assets->Layers[0].Image->Width / extent.Height;
        view.OriginPixels[0] = -int32_t(extent.Width / 2);
    }

    auto capture = [&] {
        const auto token = Draw(app);
        rhi.WaitIdle();
        return ReadColor(policy->WorldColor(token.Slot));
    };

    const auto first = capture();
    const auto extent = rhi.GetPresentationExtent();
    const auto &image = *assets->Layers.front().Image;
    const float height = extent.Height * assets->HeightFraction;
    const float width = height * image.Width / image.Height;
    const auto phases = pixel::BackgroundPhases(*assets, view, extent.Width);
    size_t verified = 0;
    for (uint32_t y = 3; y < extent.Height; y += 7) {
        for (uint32_t x = 3; x < extent.Width; x += 7) {
            std::array<float, 3> expected{.25f, .55f, .85f};
            bool boundary = false;
            for (size_t layer = 0; layer < assets->Layers.size(); ++layer) {
                float u = (x + .5f - extent.Width * .5f) / width + .5f + phases[layer];
                u = 1 - std::abs((u * .5f - std::floor(u * .5f)) * 2 - 1);
                const float v = std::clamp((y + .5f) / height, 0.f, 1.f);
                const float sx = std::clamp(u * image.Width, .5f, image.Width - .5f);
                const float sy = std::clamp(v * image.Height, .5f, image.Height - .5f);
                // Floating point differences at exact nearest-sample boundaries
                // can legitimately choose either adjacent source texel.
                boundary |= std::abs(sx - std::round(sx)) < .01f || std::abs(sy - std::round(sy)) < .01f;
                const auto tx = std::min(uint32_t(sx), image.Width - 1);
                const auto ty = std::min(uint32_t(sy), image.Height - 1);
                const auto &pixels = assets->Layers[layer].Image->Pixels;
                const size_t offset = (size_t(ty) * image.Width + tx) * 4;
                const float alpha = std::to_integer<uint8_t>(pixels[offset + 3]) / 255.f;
                for (size_t c = 0; c < 3; ++c)
                    expected[c] = Linear(std::to_integer<uint8_t>(pixels[offset + c])) * alpha + expected[c] * (1 - alpha);
            }
            if (policy->PublishedFrame().Cells[size_t(y / 16) * 128 + x / 16] == 1)
                expected = {Linear(151), Linear(104), Linear(65)};
            if (boundary) continue;
            for (size_t c = 0; c < 3; ++c)
                Require(std::abs(int(first[(size_t(y) * extent.Width + x) * 4 + c]) - int(std::lround(expected[c] * 255))) <= 2,
                        "art GPU pixels differ from decoded PNG / straight-alpha sRGB oracle");
            ++verified;
        }
    }
    Require(verified > 1000, "insufficient art GPU coverage");

    terrain = true;
    const auto terrainScene = capture();
    for (uint32_t y = extent.Height * 3 / 4; y < extent.Height; y += 13)
        for (uint32_t x = uint32_t(std::max(0, -view.OriginPixels[0])); x < extent.Width; x += 13)
            for (size_t c = 0; c < 3; ++c) {
                const std::array<uint8_t, 3> soil{151, 104, 65};
                Require(std::abs(int(terrainScene[(size_t(y) * extent.Width + x) * 4 + c]) - int(std::lround(Linear(soil[c]) * 255))) <= 1,
                        "terrain did not occlude art background");
            }

    if (const char *path = std::getenv("PIXEL_ART_CAPTURE"); path && !exactMirrorEdge) {
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << extent.Width << ' ' << extent.Height << "\n255\n";
        for (size_t i = 0; i < terrainScene.size(); i += 4)
            for (size_t c = 0; c < 3; ++c) {
                const float linear = terrainScene[i + c] / 255.f;
                file.put(char(uint8_t(std::lround(255 * (linear <= .0031308f ? 12.92f * linear : 1.055f * std::pow(linear, 1.f / 2.4f) - .055f)))));
            }
    }

    terrain = false;
    for (int i = 0; i < 4; ++i)
        Require(capture() == first, "cached art changed between frame slots");
    view.OriginPixels[0] += 512;
    Require(capture() != first, "camera motion did not move the parallax layers");

    view.OriginPixels[0] = 0;
    field = true;
    const auto diagnostic = capture();
    for (size_t i = 0; i < diagnostic.size(); i += 4)
        Require(diagnostic[i] == 255 && diagnostic[i + 1] == 128 && diagnostic[i + 2] == 128,
                "background changed F1 diagnostic pixels");
    app.Shutdown();
    std::cout << "Art GPU passed: " << verified << " PNG pixel samples, sRGB/alpha, air below image, mirror edge=" << exactMirrorEdge
              << ", terrain, frame slots, camera and F1\n";
}
} // namespace
void RunPixelDebugUISmoke();
void RunPixelFormulaUISmoke();
int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--art-only") {
            ArtBackgroundGpu();
            ArtBackgroundGpu(true);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--terrain-only") {
            TerrainMaterialsGpu();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--debug-ui-only") {
            RunPixelDebugUISmoke();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--formula-only") {
            FormulaGameplayGpu();
            RunPixelFormulaUISmoke();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--lifecycle-only") {
            Lifecycle(1);
            Lifecycle(2);
            return 0;
        }
        TerrainMaterialsGpu();
        ArtBackgroundGpu();
        ArtBackgroundGpu(true);
        WindVisualizationGpu();
        BasicWorldGameplay();
        ForceFieldDebugGameplay();
        RunPixelDebugUISmoke();
        FormulaGameplayGpu();
        RunPixelFormulaUISmoke();
        Lifecycle(1);
        Lifecycle(2);
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Pixel2D GPU smoke failed: " << error.what() << '\n';
        return 1;
    }
}
