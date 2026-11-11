#include "MonsterFrame.h"
#include <algorithm>
#include <cmath>

namespace pixel {
void BuildMonsterFrame(std::span<const ActorSnapshot> actors, const MonsterAssets &assets,
    const Camera2D &camera, std::array<int, 2> framebufferSize, WorldGridFrame &output) {
    output.MonsterDraws.clear();
    if (output.DisplayMode != GridDisplayMode::Materials || framebufferSize[0] <= 0 || framebufferSize[1] <= 0) return;
    const auto reference = camera.GetWorldPosition().Cell();
    const auto centre = camera.RelativeCellsTo(reference);
    const auto scale = output.View.CellSizePixels;
    for (const auto &actor : actors) {
        const auto position = actor.Position.RelativeCellsTo(reference);
        const float x = float((position[0] - centre[0]) * scale[0] + framebufferSize[0] * .5);
        const float y = float((position[1] - centre[1]) * scale[1] + framebufferSize[1] * .5);
        if (actor.Monster) {
            const auto &state = *actor.Monster;
            const auto &visual = assets.Require(state.Definition);
            const auto &clip = visual.Clips.at(visual.States.at(MonsterActionName(state.Action)));
            const auto frame = size_t(std::floor(double(state.ActionTicks) * clip.FramesPerSecond / 60));
            const auto index = clip.Loop ? frame % clip.Frames.size() : std::min(frame, clip.Frames.size() - 1);
            const float pixelX = scale[0] / visual.PixelsPerCell, pixelY = scale[1] / visual.PixelsPerCell;
            const float anchorX = state.FacingLeft ? visual.FrameSize[0] - visual.FootAnchor[0] : visual.FootAnchor[0];
            MonsterDraw draw{{x + actor.SizeCells[0] * scale[0] * .5f - anchorX * pixelX,
                y + actor.SizeCells[1] * scale[1] - visual.FootAnchor[1] * pixelY,
                visual.FrameSize[0] * pixelX, visual.FrameSize[1] * pixelY}, clip.Frames[index], state.FacingLeft, actor.Id.Value};
            if (draw.Bounds[0] + draw.Bounds[2] < 0 || draw.Bounds[1] + draw.Bounds[3] < 0
                || draw.Bounds[0] > framebufferSize[0] || draw.Bounds[1] > framebufferSize[1]) continue;
            output.MonsterDraws.push_back(draw);
        }
        if (actor.MaxHealth <= 0 || actor.Health <= 0 || x < -128 || x > framebufferSize[0] + 128
            || y < -128 || y > framebufferSize[1] + 128) continue;
        const float width = std::max(24.f, actor.SizeCells[0] * scale[0]);
        const float left = x + actor.SizeCells[0] * scale[0] * .5f - width * .5f;
        const float fill = width * std::clamp(actor.Health / actor.MaxHealth, 0.f, 1.f);
        output.ActorDraws.push_back({{left + width * .5f, y - 7, width * .5f + 1, 3}, {.035f, .035f, .035f, 1}, actor.Id.Value});
        output.ActorDraws.push_back({{left + fill * .5f, y - 7, fill * .5f, 2},
            actor.Kind == ActorKind::Player ? std::array{.08f, .65f, .12f, 1.f} : std::array{.75f, .09f, .04f, 1.f}, actor.Id.Value});
    }
}
}
