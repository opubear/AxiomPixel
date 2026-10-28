#include "ActorFrame.h"

namespace pixel {
void BuildActorFrame(std::span<const ActorSnapshot> actors, const Camera2D &camera,
                     std::array<int, 2> framebufferSize, WorldGridFrame &output) {
    output.ActorDraws.clear();
    if (output.DisplayMode != GridDisplayMode::Materials || framebufferSize[0] <= 0 || framebufferSize[1] <= 0) return;
    output.ActorDraws.reserve(actors.size() * TrainingDummyDrawParts);
    const auto reference = camera.GetWorldPosition().Cell();
    const auto centre = camera.RelativeTo(reference);
    const double scaleX = output.View.CellSizePixels[0], scaleY = output.View.CellSizePixels[1];
    for (const auto &actor : actors) {
        if (actor.Kind != ActorKind::TrainingDummy) continue;
        const auto position = actor.Position.RelativeTo(reference);
        const double left = (position[0] - centre[0]) * scaleX + framebufferSize[0] / 2.0;
        const double top = (position[1] - centre[1]) * scaleY + framebufferSize[1] / 2.0;
        if (left + Body::Width * scaleX < 0 || top + Body::Height * scaleY < 0
            || left > framebufferSize[0] || top > framebufferSize[1]) continue;
        const auto part = [&](double x, double y, double width, double height, std::array<float, 4> color) {
            output.ActorDraws.push_back({{float(left + (x + width * .5) * scaleX),
                float(top + (y + height * .5) * scaleY), float(width * .5 * scaleX), float(height * .5 * scaleY)},
                color, actor.Id.Value});
        };
        // Wooden head, crossbar, torso, stake and foot stay inside the 2x4 hitbox.
        part(.5, 0, 1, 1, {.65f, .36f, .13f, 1});
        part(0, 1.25, 2, .5, {.38f, .17f, .055f, 1});
        part(.5, 1, 1, 1.5, {.52f, .25f, .075f, 1});
        part(.85, 2.5, .3, 1.25, {.25f, .10f, .03f, 1});
        part(.15, 3.75, 1.7, .25, {.38f, .17f, .055f, 1});
    }
}
} // namespace pixel
