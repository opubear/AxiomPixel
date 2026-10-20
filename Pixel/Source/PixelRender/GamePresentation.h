#pragma once
#include "Camera/GameCamera.h"
#include "Gameplay/GameSession.h"
#include "PixelWorldPresentation.h"
#include <functional>
#include <string_view>

namespace pixel {
class ArtAssetStore;
class GamePresentation {
  public:
    GamePresentation();
    CellAppearanceTable Appearances;
    PostProcessParameters Post;
    WindVisualSettings WindVisual;
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;
    void ToggleTint();
    void ToggleFieldView();
    // The consumer owns its snapshot. Session, camera and this object must be
    // alive when invoked; releasing the callback never accesses them. Never called from Draw.
    std::function<void(WorldGridFrame &)> CreateFramePublisher(GameSession &session, const GameCamera &camera) const;
    static GridTextureCatalog MakeTextures();
    // Loads Content textures and atomically replaces the corresponding material mapping.
    GridTextureCatalog LoadTextures(ArtAssetStore &assets, std::string_view manifest = "ArtAssets.json");
};
} // namespace pixel
