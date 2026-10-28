#include "GamePresentation.h"
#include "FormulaFrame.h"
#include "DeviceFrame.h"
#include "ActorFrame.h"
#include "CharacterAnimation.h"
#include "ForceGardenFrame.h"
#include "Runtime/Render/Assets/ArtAssets.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace pixel {
namespace {
constexpr std::array RequiredMaterials{
    uint32_t(CellTypeId::Air), uint32_t(CellTypeId::Dirt), uint32_t(CellTypeId::GroundSoil),
    uint32_t(CellTypeId::GrassSoil), uint32_t(CellTypeId::Stone), uint32_t(CellTypeId::CoalOre),
    uint32_t(CellTypeId::CopperOre), uint32_t(CellTypeId::IronOre), uint32_t(CellTypeId::TungstenOre),
    uint32_t(CellTypeId::GoldOre), uint32_t(CellTypeId::Water),
    uint32_t(CellTypeId::Steam), uint32_t(CellTypeId::Fire)};

CellAppearanceTable DefaultAppearances() {
    return {{uint32_t(CellTypeId::Air), 0}, {uint32_t(CellTypeId::Dirt), 1},
            {uint32_t(CellTypeId::GroundSoil), 1}, {uint32_t(CellTypeId::GrassSoil), {1, 2}},
            {uint32_t(CellTypeId::Stone), 3}, {uint32_t(CellTypeId::CoalOre), 4},
            {uint32_t(CellTypeId::CopperOre), 5}, {uint32_t(CellTypeId::IronOre), 6},
            {uint32_t(CellTypeId::TungstenOre), 7}, {uint32_t(CellTypeId::GoldOre), 8},
            {uint32_t(CellTypeId::Water), {9, 10, 3}},
            {uint32_t(CellTypeId::Steam), 11}, {uint32_t(CellTypeId::Fire), 12}};
}

uint64_t PublicationCapacity(WorldRect region) {
    const uint64_t cells = uint64_t(region.Width) * region.Height;
    const uint64_t regions = uint64_t(region.Width / storage::RegionEdge) * (region.Height / storage::RegionEdge);
    return CommittedWorldView::CapacityFor(region) + cells * 2 * sizeof(uint32_t) + regions * 512 + 2048
        + 2 * api::GameplayObservation::MaximumRetainedBytes
        + Actors::MaximumCount * TrainingDummyDrawParts * sizeof(ActorDraw);
}

WorldRect LimitPublication(WorldRect region, uint64_t budget) {
    const uint32_t minimum = storage::RegionEdge;
    if (region.Width <= 2048 && region.Height <= 2048 && PublicationCapacity(region) <= budget)
        return region;
    const double factor = std::min({1.0, 2048.0 / region.Width, 2048.0 / region.Height,
                                    std::sqrt(double(budget) / double(PublicationCapacity(region)))});
    const uint32_t width = std::max(minimum, uint32_t(region.Width * factor) / minimum * minimum);
    const uint32_t height = std::max(minimum, uint32_t(region.Height * factor) / minimum * minimum);
    region.X += int64_t((region.Width - width) / 2 / minimum) * minimum;
    region.Y += int64_t((region.Height - height) / 2 / minimum) * minimum;
    region.Width = width;
    region.Height = height;
    return region;
}
} // namespace
GamePresentation::GamePresentation() : Appearances(DefaultAppearances()) {}

void GamePresentation::ToggleTint() { Post.Values[1] = Post.Values[1] == 1 ? .35f : 1; }
void GamePresentation::ToggleFieldView() {
    DisplayMode = DisplayMode == GridDisplayMode::Materials ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
}
std::function<void(WorldGridFrame &)> GamePresentation::CreateFramePublisher(GameSession &session, const GameCamera &camera,
    std::shared_ptr<const CharacterAssets> characterArt) {
    auto snapshot = session.CreateReadView();
    return [this, &session, &camera, snapshot = std::move(snapshot), characterArt = std::move(characterArt),
            animation = CharacterAnimation{}](WorldGridFrame &frame) mutable {
        auto &world = session.GameWorld;
        const auto lifetime = world.PublicationLifetime();
        frame.TrackPublication(world.GetMap().GetIdentity(), [&world, lifetime, key = &frame] {
            if (!lifetime.expired()) world.ReleasePublication(key);
        });
        const auto requested = LimitPublication(VisibleWorldRegion(camera.GetView(), camera.GetFramebufferSize()),
                                                world.PublicationBudget());
        session.RefreshReadView(*snapshot, requested);
        if (!snapshot->IsValid()) {
            // A newly attached consumer during a pending step has no committed
            // samples yet. It must not fill the gap from the live solver.
            frame.View = camera.GetView();
            frame.SetRegion({requested.X, requested.Y}, Grid2D::RegionSize, Grid2D::RegionSize);
            frame.Post = Post;
            frame.DisplayMode = GridDisplayMode::Materials;
            std::fill(frame.Cells().begin(), frame.Cells().end(), Grid2D::MissingCell);
            frame.PrepareRegionVersions(0x6d697373696e67ULL);
            frame.SetSourceRegionVersion(0, 0);
            frame.Character.reset();
            frame.FormulaDraws.clear();
            frame.DeviceDraws.clear();
            frame.GardenDraws.clear();
            frame.ActorDraws.clear();
            frame.HasWind = false;
            frame.MissingOutsideRegion = true;
            GameplayFrame.reset();
            return;
        }
        const auto region = snapshot->Bounds();
        const uint64_t cells = uint64_t(region.Width) * region.Height;
        const uint64_t regions = uint64_t(region.Width / storage::RegionEdge) * (region.Height / storage::RegionEdge);
        const uint64_t capacity = cells * 8 + regions * 512 + 2048
            + Actors::MaximumCount * TrainingDummyDrawParts * sizeof(ActorDraw)
            + snapshot->Gameplay().RetainedBytes() + (GameplayFrame ? GameplayFrame->RetainedBytes() : 0);
        const bool resizing = frame.GetWidth() != region.Width || frame.GetHeight() != region.Height;
        const auto previous = world.PublicationBytesFor(&frame);
        if (!world.ReservePublicationBytes(&frame, capacity + (resizing ? previous : 0))) return;
        session.SynchronizeReadViews();
        frame.View = camera.GetView();
        frame.FormulaDraws.clear();
        frame.DeviceDraws.clear();
        frame.GardenDraws.clear();
        frame.ActorDraws.clear();
        frame.Character.reset();
        frame.SetRegion({region.X, region.Y}, region.Width, region.Height);
        frame.Post = Post;
        frame.MissingOutsideRegion = !snapshot->IsFinite();
        if (DisplayMode == GridDisplayMode::ForceField)
            BuildForceFieldFrame(*snapshot, frame);
        else {
            BuildWorldGridFrame(snapshot->ReadState(), Appearances, frame);
            BuildFormulaFrame(snapshot->Projectiles(), camera.GetCamera(), camera.GetFramebufferSize(), frame);
            BuildDeviceFrame(snapshot->GetDevices(), camera.GetCamera(), camera.GetFramebufferSize(), frame);
            BuildForceGardenFrame(snapshot->Gameplay().Garden, camera.GetCamera(), camera.GetFramebufferSize(), frame);
            BuildActorFrame(snapshot->GetActors(), camera.GetCamera(), camera.GetFramebufferSize(), frame);
        }
        BuildWindVisualFrame(*snapshot, WindVisual, float(std::fmod(snapshot->SimulationTime(), 2.0)), frame);
        if (snapshot->GetPlayer()) {
            BuildCharacterFrame(*snapshot->GetPlayer(), camera.GetCamera(), camera.GetFramebufferSize(), frame);
            animation.Update(*snapshot->GetPlayer(), world.GetMap().GetIdentity(), snapshot->SimulationTime());
            if (characterArt)
                animation.Apply(*characterArt, frame);
        }
        // Move an exact copy into the cache so retained vector capacities stay
        // bounded; UI reads these same commit values even if an action follows.
        auto gameplay = snapshot->Gameplay();
        GameplayFrame = std::move(gameplay);
        world.SetPublicationBytes(&frame, frame.GetMemoryUsage().ReservedBytes() + GameplayFrame->RetainedBytes());
    };
}

GridTextureCatalog GamePresentation::MakeTextures() {
    // Terrain textures only; the character uses a separate composition pass.
    GridTextureCatalog textures{1, 1, 13, {}};
    for (const auto color : {std::array{0, 0, 0, 0}, std::array{151, 104, 65, 255},
                            std::array{102, 143, 58, 255}, std::array{111, 113, 114, 255},
                            std::array{49, 48, 47, 255}, std::array{167, 104, 57, 255},
                            std::array{171, 146, 125, 255}, std::array{89, 106, 115, 255},
                            std::array{207, 166, 48, 255}, std::array{38, 102, 166, 180},
                            std::array{92, 167, 196, 210}, std::array{216, 229, 232, 100},
                            std::array{255, 137, 35, 255}})
        for (int channel : color)
            textures.Pixels.push_back(std::byte(channel));
    return textures;
}

GridTextureCatalog GamePresentation::LoadTextures(ArtAssetStore &assets, std::string_view manifest) {
    const auto terrain = assets.LoadTerrain(manifest, RequiredMaterials);
    const auto &first = *terrain->Images.front().Image;
    GridTextureCatalog textures{first.Width, first.Height, uint32_t(terrain->Images.size() + 1), {}};
    textures.CellsPerTile = terrain->CellsPerTile;
    const size_t layerBytes = first.Pixels.size();
    textures.Pixels.resize(layerBytes * textures.Layers); // transparent layer zero
    for (const auto &entry : terrain->Images)
        std::copy(entry.Image->Pixels.begin(), entry.Image->Pixels.end(), textures.Pixels.begin() + entry.Layer * layerBytes);

    CellAppearanceTable appearances;
    for (const auto &material : terrain->Materials)
        appearances.emplace(material.TypeId, CellAppearance{material.Layer, material.SurfaceLayer, material.SurfaceStrip});
    ValidateGridTextureCatalog(textures);
    Appearances = std::move(appearances);
    return textures;
}
} // namespace pixel
