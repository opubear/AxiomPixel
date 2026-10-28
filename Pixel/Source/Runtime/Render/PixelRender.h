#pragma once

#include "HAL/RenderHardwareInterface/RenderHardwareInterface.h"
#include "Passes/RenderPasses.h"
#include "Render/RenderPolicy/RenderPolicyInterface.h"
#include "Runtime/Render/Assets/ArtAssets.h"
#include "WorldGridFrame.h"
#include "UI/InventoryOverlay.h"
#include <functional>
#include <memory>
#include <string>

// The publisher fills owned CPU data once at the start of a drawable frame.
class PixelRender final : public RenderPolicyInterface {
  public:
    PixelRender(DynamicRHI &rhi, GridTextureCatalog catalog,
                std::function<void(WorldGridFrame &)> input,
                std::string postShader = "Pixel2DPostProcess.fs",
                std::shared_ptr<const pixel::BackgroundAssets> background = {},
                std::shared_ptr<const pixel::CharacterAssets> character = {},
                pixel::render::InventoryOverlay::Publisher ui = {});
    ~PixelRender() override;
    void Initialize() override;
    void FrameBegin() override;
    void Draw() override;
    void OnPresentationChanged(const RHIPresentationChanged &) override;
    void Clear() override;
    uint64_t SubmittedFrames() const { return FrameCount; }
    uint64_t UploadedWorldBytes() const { return WorldUploadBytes; }
    pixel::performance::MemoryUsage FormulaPresentationMemory() const {
        return Published ? Published->GetFormulaMemoryUsage() : pixel::performance::MemoryUsage{};
    }
    pixel::performance::MemoryUsage DevicePresentationMemory() const {
        return Published ? Published->GetDeviceMemoryUsage() : pixel::performance::MemoryUsage{};
    }
    uint64_t FormulaTargetBytes() const { return uint64_t(Resources.Extent.Width) * Resources.Extent.Height * 4 * Resources.Frames.size(); }
    const RHITexture &SceneColor(uint32_t slot) const;
    const RHITexture &WorldColor(uint32_t slot) const;
    // Borrowed until the next FrameBegin or Clear, including after Draw.
    WorldGridFrameView PublishedFrame() const;

  private:
    DynamicRHI &RHI;
    GridTextureCatalog Catalog;
    std::function<void(WorldGridFrame &)> Input;
    std::unique_ptr<WorldGridFrame> Published;
    bool FrameReady = false;
    std::string PostShader;
    std::shared_ptr<const pixel::BackgroundAssets> Background;
    std::shared_ptr<const pixel::CharacterAssets> CharacterArt;
    pixel::render::RenderResources Resources;
    pixel::render::UploadPass Upload;
    pixel::render::WorldPass World;
    pixel::render::WindPass Wind;
    pixel::render::SceneObjectsPass SceneObjects;
    pixel::render::CharacterPass Character;
    pixel::render::PostProcessPass PostProcess;
    pixel::render::InventoryOverlay InventoryUI;
    pixel::render::InventoryOverlay::Publisher UIPublisher;
    uint64_t FrameCount = 0;
    uint64_t WorldUploadBytes = 0;
    void CreateColorTargets();
};
