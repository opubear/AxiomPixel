#include "ItemFrame.h"

namespace pixel {
void BuildItemFrame(std::span<const api::DroppedItemObservation> items, const Camera2D &camera,
                    std::array<int, 2> framebuffer, WorldGridFrame &output) {
    output.ItemDraws.clear();
    if (output.DisplayMode != GridDisplayMode::Materials || framebuffer[0] <= 0 || framebuffer[1] <= 0) return;
    const auto reference = camera.GetWorldPosition().Cell();
    const auto center = camera.RelativeCellsTo(reference);
    const double scaleX = output.View.CellSizePixels[0], scaleY = output.View.CellSizePixels[1];
    for (const auto &item : items) {
        const auto position = WorldPosition::FromCell({item.Location.AnchorCell.X, item.Location.AnchorCell.Y}, item.Location.Offset);
        const auto point = position.RelativeCellsTo(reference);
        const double x = (point[0] - center[0]) * scaleX + framebuffer[0] / 2.0;
        const double y = (point[1] - center[1]) * scaleY + framebuffer[1] / 2.0;
        if (x < -scaleX || y < -scaleY || x > framebuffer[0] + scaleX || y > framebuffer[1] + scaleY) continue;
        // A pale outline keeps the small wooden pickup legible against soil.
        const auto part = [&](double dx, double dy, double rx, double ry, std::array<float, 4> color) {
            output.ItemDraws.push_back({{float(x + dx * scaleX), float(y + dy * scaleY),
                                         float(rx * scaleX), float(ry * scaleY)}, color, item.Item.Instance});
        };
        part(0, 0, .7, .21, {.86f, .66f, .3f, 1});
        part(0, 0, .6, .12, {.32f, .14f, .045f, 1});
        part(.27, -.22, .08, .22, {.53f, .29f, .1f, 1});
    }
}
} // namespace pixel
