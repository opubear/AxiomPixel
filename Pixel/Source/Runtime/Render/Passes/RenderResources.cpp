#include "RenderResources.h"

namespace pixel::render {
void RenderPassContext::BindFullscreen(RHICommandList &commands) const {
    const auto extent = Resources.Extent;
    commands.SetViewport({0, 0, float(extent.Width), float(extent.Height), 0, 1});
    commands.SetScissor({0, 0, extent.Width, extent.Height});
    commands.BindVertexBuffer(*Resources.Vertices);
    commands.BindIndexBuffer(*Resources.Indices);
}
} // namespace pixel::render
