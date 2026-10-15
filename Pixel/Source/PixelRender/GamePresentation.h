#pragma once
#include "Camera/GameCamera.h"
#include "Gameplay/GameSession.h"
#include "PixelWorldPresentation.h"
#include <functional>

namespace pixel {
class GamePresentation {
  public:
    CellAppearanceTable Appearances{{static_cast<uint32_t>(CellTypeId::Dirt), 1}};
    PostProcessParameters Post;
    WindVisualSettings WindVisual;
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;
    void ToggleTint();
    void ToggleFieldView();
    // The consumer owns its snapshot. Session, camera and this object must be
    // alive when invoked; releasing the callback never accesses them. Never called from Draw.
    std::function<void(WorldGridFrame &)> CreateFramePublisher(GameSession &session, const GameCamera &camera) const;
    static GridTextureCatalog MakeTextures();
};
} // namespace pixel
