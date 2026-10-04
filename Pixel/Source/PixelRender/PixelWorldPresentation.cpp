#include "PixelWorldPresentation.h"
#include "World/World.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void ValidatePresentationWorld(pixel::WorldStateView world) {
    if (world.Width != Grid2D::Width || world.Height != Grid2D::Height || world.Cells.size() != Grid2D::CellCount)
        throw std::invalid_argument("2D presentation requires a 1024x1024 gameplay world");
}
} // namespace
void BuildWorldGridFrame(pixel::WorldStateView world, const CellAppearanceTable &appearances, WorldGridFrame &output) {
    ValidatePresentationWorld(world);
    output.DisplayMode = GridDisplayMode::Materials;
    auto cells = output.Cells();
    for (size_t index = 0; index < world.Cells.size(); ++index) {
        const auto type = world.Cells[index].Type.Value;
        if (!type) {
            cells[index] = 0;
            continue;
        }
        const auto appearance = appearances.find(type);
        if (appearance == appearances.end())
            throw std::invalid_argument("gameplay cell type has no appearance mapping");
        cells[index] = appearance->second;
    }
}

std::array<float, 3> FieldDirectionColor(pixel::WindVelocity velocity) {
    if (!std::isfinite(velocity.X) || !std::isfinite(velocity.Y))
        throw std::invalid_argument("field direction must be finite");
    const double length = std::hypot(double(velocity.X), double(velocity.Y));
    if (length == 0)
        return {0.5f, 0.5f, 0.5f};
    return {float(0.5 + 0.5 * velocity.X / length), float(0.5 + 0.5 * velocity.Y / length), 0.5f};
}

void BuildForceFieldFrame(const pixel::World &world, WorldGridFrame &output) {
    ValidatePresentationWorld(world.ReadState());
    auto cells = output.Cells();
    for (uint32_t y = 0; y < Grid2D::Height; ++y)
        for (uint32_t x = 0; x < Grid2D::Width; ++x) {
            const auto color = FieldDirectionColor(world.GetFieldVelocity(x, y));
            uint32_t packed = 0;
            for (size_t channel = 0; channel < color.size(); ++channel)
                packed |= uint32_t(std::lround(std::clamp(color[channel], 0.f, 1.f) * 255)) << (channel * 8);
            cells[size_t(y) * Grid2D::Width + x] = packed;
        }
    output.DisplayMode = GridDisplayMode::ForceField;
    // Debug colours must not inherit gameplay tint; switching back restores it.
    output.Post = {};
}
