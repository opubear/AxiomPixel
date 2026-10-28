#pragma once
#include "Runtime/System/IO/Input/PresentationControl.h"
#include "Runtime/Gameplay/Camera/GameCamera.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/Gameplay/GameAPI/GameplayObservations.h"
#include "PixelWorldPresentation.h"
#include <functional>
#include <string_view>

namespace pixel {
class ArtAssetStore;
struct CharacterAssets;
class GamePresentation : public PresentationControl {
  public:
    GamePresentation();
    CellAppearanceTable Appearances;
    PostProcessParameters Post;
    WindVisualSettings WindVisual;
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;
    void ResetPostProcessing() override { Post = {}; }
    void ToggleTint() override;
    void ToggleFieldView() override;
    // The consumer owns its snapshot. Session, camera and this object must be
    // alive when invoked; releasing the callback never accesses them. Never called from Draw.
    std::function<void(WorldGridFrame &)> CreateFramePublisher(GameSession &session, const GameCamera &camera,
        std::shared_ptr<const CharacterAssets> characterArt = {});
    // Updated with terrain/device publication, never by UI actions or Draw.
    // Each presentation consumer owns its values until its next published frame.
    const std::optional<api::GameplayObservation> &PublishedGameplay() const { return GameplayFrame; }
    static GridTextureCatalog MakeTextures();
    // Loads Content textures and atomically replaces the corresponding material mapping.
    GridTextureCatalog LoadTextures(ArtAssetStore &assets, std::string_view manifest = "ArtAssets.json");
  private:
    std::optional<api::GameplayObservation> GameplayFrame;
};
} // namespace pixel
