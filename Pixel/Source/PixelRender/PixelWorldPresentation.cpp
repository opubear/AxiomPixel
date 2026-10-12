#include "PixelWorldPresentation.h"
#include "World/World.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void ValidatePresentationWorld(pixel::WorldStateView world) {
    if (world.Width != Grid2D::Width || world.Height != Grid2D::Height || world.Cells.size() != Grid2D::CellCount)
        throw std::invalid_argument("2D presentation requires a 1024x1024 gameplay world");
}
} // namespace
void BuildWorldGridFrame(pixel::WorldStateView world, const CellAppearanceTable &appearances, WorldGridFrame &output) {
    ValidatePresentationWorld(world);
    output.Character.reset();
    output.DisplayMode = GridDisplayMode::Materials;
    auto cells = output.Cells();
    for (size_t index = 0; index < world.Cells.size(); ++index) {
        const auto type = static_cast<uint32_t>(world.Cells[index].Type);
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
    output.Character.reset();
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

void BuildCharacterFrame(const pixel::Character &character, const pixel::Camera2D &camera,
                         std::array<int, 2> framebufferSize, WorldGridFrame &output) {
    output.Character.reset();
    if (output.DisplayMode != GridDisplayMode::Materials)
        return;
    auto linearColor = [](std::array<int, 3> rgb) {
        std::array<float, 4> color{0, 0, 0, 1};
        for (size_t channel = 0; channel < 3; ++channel) {
            const float value = rgb[channel] / 255.f;
            color[channel] = value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }
        return color;
    };
    CharacterDraw draw;
    draw.Bounds = {
        float((double(character.X) - camera.GetPosition()[0]) * output.View.CellSizePixels[0] + framebufferSize[0] / 2.0),
        float((double(character.Y) - camera.GetPosition()[1]) * output.View.CellSizePixels[1] + framebufferSize[1] / 2.0),
        pixel::Character::Width * output.View.CellSizePixels[0],
        pixel::Character::Height * output.View.CellSizePixels[1]};
    draw.HeadColor = linearColor({255, 212, 140});
    draw.BodyColor = linearColor({40, 210, 180});
    output.Character = draw;
}

GridView2D MakeCameraView(const pixel::Camera2D &camera, std::array<int, 2> framebufferSize,
                          GridView2D settings) {
    for (size_t axis = 0; axis < 2; ++axis) {
        if (settings.CellSizePixels[axis] == 0 || framebufferSize[axis] < 0)
            throw std::invalid_argument("camera projection requires nonnegative viewport and positive cell size");
        const double origin = std::round(camera.GetPosition()[axis] * settings.CellSizePixels[axis] - framebufferSize[axis] / 2.0);
        settings.OriginPixels[axis] = int32_t(std::clamp(origin, double(std::numeric_limits<int32_t>::min()),
                                                       double(std::numeric_limits<int32_t>::max())));
    }
    return settings;
}
