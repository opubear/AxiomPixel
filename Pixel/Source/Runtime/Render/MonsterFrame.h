#pragma once
#include "Assets/ArtAssets.h"
#include "Runtime/Gameplay/Actors/ActorState.h"
#include "Runtime/Gameplay/Camera/Camera2D.h"
#include "WorldGridFrame.h"

namespace pixel {
void BuildMonsterFrame(std::span<const ActorSnapshot> actors, const MonsterAssets &assets,
    const Camera2D &camera, std::array<int, 2> framebufferSize, WorldGridFrame &output);
}
