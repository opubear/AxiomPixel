#include "CharacterAnimation.h"
#include <algorithm>
#include <cmath>

namespace pixel {
void CharacterAnimation::Update(const ActorSnapshot &actor, uint64_t worldIdentity, double simulationTime) {
    const bool reset = !Initialized || WorldIdentity != worldIdentity || Actor != actor.Id || simulationTime < LastTime;
    if (reset) {
        WorldIdentity = worldIdentity;
        Actor = actor.Id;
        Position = actor.Position;
        LastTime = ClipTime = simulationTime;
        CurrentMotion = actor.Grounded ? CharacterMotion::Idle : CharacterMotion::Fall;
        Left = false;
        Initialized = true;
        return;
    }
    if (simulationTime == LastTime)
        return; // Paused or another drawable frame of the same commit.

    const auto reference = Position.Cell();
    const auto previous = Position.RelativeTo(reference), current = actor.Position.RelativeTo(reference);
    const double dx = current[0] - previous[0], dy = current[1] - previous[1];
    if (std::abs(dx) > 1e-6)
        Left = dx < 0;
    const auto motion = !actor.Grounded ? (dy < -1e-6 ? CharacterMotion::Jump : CharacterMotion::Fall)
        : (std::abs(dx) > 1e-6 ? CharacterMotion::Run : CharacterMotion::Idle);
    if (motion != CurrentMotion) {
        CurrentMotion = motion;
        ClipTime = simulationTime;
    }
    Position = actor.Position;
    LastTime = simulationTime;
}

void CharacterAnimation::Apply(const CharacterAssets &assets, WorldGridFrame &frame) const {
    if (!frame.Character)
        return;
    auto &draw = *frame.Character;
    const auto &clip = assets.Animations[size_t(CurrentMotion)];
    const double elapsedFrames = std::floor(std::max(0.0, LastTime - ClipTime) * clip.FramesPerSecond + 1e-8);
    const auto index = uint32_t(clip.Loop ? std::fmod(elapsedFrames, clip.FrameCount)
                                        : std::min(elapsedFrames, double(clip.FrameCount - 1)));
    const float scaleX = frame.View.CellSizePixels[0] / assets.PixelsPerCell;
    const float scaleY = frame.View.CellSizePixels[1] / assets.PixelsPerCell;
    const float anchorX = Left ? assets.FrameSize[0] - assets.FootAnchor[0] : assets.FootAnchor[0];
    draw.Bounds = {draw.Bounds[0] + draw.Bounds[2] * .5f - anchorX * scaleX,
        draw.Bounds[1] + draw.Bounds[3] - assets.FootAnchor[1] * scaleY,
        assets.FrameSize[0] * scaleX, assets.FrameSize[1] * scaleY};
    draw.FrameTexels = {float((clip.FirstFrame + index) * assets.FrameSize[0]), float(clip.Row * assets.FrameSize[1]),
        float(assets.FrameSize[0]), float(assets.FrameSize[1])};
    draw.Sprite = {float(assets.Layers.size()), Left ? 1.f : 0.f, 0, 0};
}
} // namespace pixel
