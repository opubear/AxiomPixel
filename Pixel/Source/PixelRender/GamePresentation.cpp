#include "GamePresentation.h"
#include <cmath>
#include <memory>

namespace pixel {
static_assert(GameSession::WorldWidth == Grid2D::Width && GameSession::WorldHeight == Grid2D::Height);
void GamePresentation::ToggleTint() { Post.Values[1] = Post.Values[1] == 1 ? .35f : 1; }
void GamePresentation::ToggleFieldView() {
    DisplayMode = DisplayMode == GridDisplayMode::Materials ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
}
std::function<void(WorldGridFrame &)> GamePresentation::CreateFramePublisher(GameSession &session, const GameCamera &camera) const {
    const auto state = session.GetWorld().ReadState();
    auto snapshot = std::make_shared<WorldState>(state.Width, state.Height);
    return [this, &session, &camera, snapshot = std::move(snapshot)](WorldGridFrame &frame) {
        auto &world = session.GetWorld();
        world.PublishState(*snapshot);
        frame.View = camera.GetView();
        frame.Post = Post;
        if (DisplayMode == GridDisplayMode::ForceField)
            BuildForceFieldFrame(world, frame);
        else
            BuildWorldGridFrame(snapshot->Read(), Appearances, frame);
        BuildWindVisualFrame(world, WindVisual, float(std::fmod(session.GetSimulationTime(), 2.0)), frame);
        if (world.GetPlayer())
            BuildCharacterFrame(*world.GetPlayer(), camera.GetCamera(), camera.GetFramebufferSize(), frame);
    };
}
GridTextureCatalog GamePresentation::MakeTextures() {
    // Terrain textures only; the character uses a separate continuous-colour draw.
    GridTextureCatalog textures{1, 1, 2, {}};
    for (const auto color : {std::array{0, 0, 0, 0}, std::array{151, 104, 65, 255}})
        for (int channel : color)
            textures.Pixels.push_back(std::byte(channel));
    return textures;
}
} // namespace pixel
