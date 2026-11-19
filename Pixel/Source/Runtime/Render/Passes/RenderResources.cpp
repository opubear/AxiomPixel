#include "RenderResources.h"
#include <algorithm>

namespace pixel::render {
void RenderPassContext::BindFullscreen(RHICommandList &commands) const {
    const auto extent = Resources.Extent;
    commands.SetViewport({0, 0, float(extent.Width), float(extent.Height), 0, 1});
    commands.SetScissor({0, 0, extent.Width, extent.Height});
    commands.BindVertexBuffer(*Resources.Vertices);
    commands.BindIndexBuffer(*Resources.Indices);
}

void StageGridUpload(RHIUploadBuffer &upload, const RHITextureDesc &texture, std::span<const std::byte> texels,
                     uint32_t tileEdge, std::span<const uint64_t> versions, std::span<const uint64_t> uploaded,
                     std::vector<RHITextureCopyRegion> &dirty) {
    constexpr size_t TexelBytes = 4;
    dirty.clear();
    if (versions.empty() || uploaded.size() != versions.size()) {
        upload.Write(texels);
        dirty.push_back({0, 0, 0, 0, 0, texture.Width, texture.Height});
        return;
    }
    const uint32_t columns = (texture.Width + tileEdge - 1) / tileEdge;
    for (size_t index = 0; index < versions.size(); ++index) {
        if (versions[index] == uploaded[index]) continue;
        const uint32_t x = uint32_t(index % columns) * tileEdge;
        const uint32_t y = uint32_t(index / columns) * tileEdge;
        const uint32_t width = std::min(tileEdge, texture.Width - x);
        const uint32_t height = std::min(tileEdge, texture.Height - y);
        for (uint32_t row = 0; row < height; ++row) {
            const size_t offset = (size_t(y + row) * texture.Width + x) * TexelBytes;
            upload.WriteRegion(texels.subspan(offset, width * TexelBytes), offset);
        }
        dirty.push_back({(size_t(y) * texture.Width + x) * TexelBytes, texture.Width, texture.Height, x, y, width, height});
    }
}
} // namespace pixel::render
