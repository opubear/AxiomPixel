#pragma once
#include "WorldGridFrame.h"
#include "Runtime/Gameplay/GameAPI/GameplayObservations.h"
#include "Runtime/Gameplay/Camera/Camera2D.h"

namespace pixel {
inline constexpr size_t DroppedItemDrawParts = 3;
void BuildItemFrame(std::span<const api::DroppedItemObservation> items, const Camera2D &camera,
                    std::array<int, 2> framebuffer, WorldGridFrame &output);
} // namespace pixel
