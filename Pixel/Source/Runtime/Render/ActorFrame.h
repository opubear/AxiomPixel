#pragma once
#include "WorldGridFrame.h"
#include "Runtime/Gameplay/Actors/Actors.h"
#include "Runtime/Gameplay/Camera/Camera2D.h"

namespace pixel {
inline constexpr size_t TrainingDummyDrawParts = 5;
void BuildActorFrame(std::span<const ActorSnapshot> actors, const Camera2D &camera,
                     std::array<int, 2> framebufferSize, WorldGridFrame &output);
} // namespace pixel
