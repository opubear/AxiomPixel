#include "GamePresentation.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace pixel {
namespace {
uint64_t PublicationCapacity(WorldRect region) {
    const uint64_t cells = uint64_t(region.Width) * region.Height;
    const uint64_t regions = uint64_t(region.Width / storage::RegionEdge) * (region.Height / storage::RegionEdge);
    return cells * (sizeof(CellState) + 2 * sizeof(uint32_t)) + regions * 1024 + 4096;
}

WorldRect LimitPublication(WorldRect region, uint64_t budget) {
    const uint32_t minimum = storage::RegionEdge;
    if (PublicationCapacity(region) <= budget)
        return region;
    const double factor = std::sqrt(double(budget) / double(PublicationCapacity(region)));
    const uint32_t width = std::max(minimum, uint32_t(region.Width * factor) / minimum * minimum);
    const uint32_t height = std::max(minimum, uint32_t(region.Height * factor) / minimum * minimum);
    region.X += int64_t((region.Width - width) / 2 / minimum) * minimum;
    region.Y += int64_t((region.Height - height) / 2 / minimum) * minimum;
    region.Width = width;
    region.Height = height;
    return region;
}
} // namespace
void GamePresentation::ToggleTint() { Post.Values[1] = Post.Values[1] == 1 ? .35f : 1; }
void GamePresentation::ToggleFieldView() {
    DisplayMode = DisplayMode == GridDisplayMode::Materials ? GridDisplayMode::ForceField : GridDisplayMode::Materials;
}
std::function<void(WorldGridFrame &)> GamePresentation::CreateFramePublisher(GameSession &session, const GameCamera &camera) const {
    auto &owner = session.GetWorld();
    const auto lifetime = owner.PublicationLifetime();
    auto snapshot = std::shared_ptr<WorldSnapshot>(new WorldSnapshot, [&owner, lifetime](WorldSnapshot *value) {
        if (!lifetime.expired())
            owner.ReleasePublication(value);
        delete value;
    });
    return [this, &session, &camera, snapshot = std::move(snapshot)](WorldGridFrame &frame) {
        auto &world = session.GetWorld();
        const auto lifetime = world.PublicationLifetime();
        frame.TrackPublication(world.GetMap().GetIdentity(), [&world, lifetime, key = &frame] {
            if (!lifetime.expired())
                world.ReleasePublication(key);
        });
        frame.View = camera.GetView();
        const auto region = LimitPublication(VisibleWorldRegion(frame.View, camera.GetFramebufferSize()), world.PublicationBudget());
        const bool resizing = frame.GetWidth() != region.Width || frame.GetHeight() != region.Height;
        const uint64_t oldCells = uint64_t(frame.GetWidth()) * frame.GetHeight();
        const uint64_t newCells = uint64_t(region.Width) * region.Height;
        // Snapshot rows are discarded before replacement. Vector replacement can
        // temporarily retain both old texture planes and one new plane.
        const uint64_t peak = resizing ? std::max(oldCells * 4, oldCells * 8 > newCells * 4 ? oldCells * 8 - newCells * 4 : 0) : 0;
        const uint64_t regions = uint64_t(region.Width / storage::RegionEdge) * (region.Height / storage::RegionEdge);
        const uint64_t snapshotCapacity = newCells * sizeof(CellState) + regions * 512 + 2048;
        const uint64_t frameCapacity = newCells * 8 + regions * 512 + 2048;
        const auto oldSnapshotCapacity = world.PublicationBytesFor(snapshot.get());
        const bool reservedSnapshot = world.ReservePublicationBytes(snapshot.get(), std::max(snapshotCapacity, oldSnapshotCapacity));
        const bool reservedFrame = reservedSnapshot && world.ReservePublicationBytes(&frame, frameCapacity + peak);
        if (!reservedFrame) {
            if (reservedSnapshot)
                world.SetPublicationBytes(snapshot.get(), oldSnapshotCapacity);
            world.RequestVisibleRegion(region);
            frame.MissingOutsideRegion = !world.GetMap().IsFinite();
            if (!snapshot->Read().IsValid()) {
                frame.SetRegion({region.X, region.Y}, frame.GetWidth(), frame.GetHeight());
                std::fill(frame.Cells().begin(), frame.Cells().end(), Grid2D::MissingCell);
                frame.Character.reset();
                frame.HasWind = false;
            } else if (world.GetPlayer()) {
                BuildCharacterFrame(*world.GetPlayer(), camera.GetCamera(), camera.GetFramebufferSize(), frame);
            }
            world.SetPublicationBytes(snapshot.get(), snapshot->GetMemoryUsage().ReservedBytes());
            world.SetPublicationBytes(&frame, frame.GetMemoryUsage().ReservedBytes());
            return;
        }
        world.RequestVisibleRegion(region);
        world.PublishSnapshot(region, *snapshot);
        frame.SetRegion({region.X, region.Y}, region.Width, region.Height);
        frame.Post = Post;
        frame.MissingOutsideRegion = !world.GetMap().IsFinite();
        if (DisplayMode == GridDisplayMode::ForceField)
            BuildForceFieldFrame(world, frame);
        else
            BuildWorldGridFrame(snapshot->Read(), Appearances, frame);
        BuildWindVisualFrame(world, WindVisual, float(std::fmod(session.GetSimulationTime(), 2.0)), frame);
        if (world.GetPlayer())
            BuildCharacterFrame(*world.GetPlayer(), camera.GetCamera(), camera.GetFramebufferSize(), frame);
        world.SetPublicationBytes(snapshot.get(), snapshot->GetMemoryUsage().ReservedBytes());
        world.SetPublicationBytes(&frame, frame.GetMemoryUsage().ReservedBytes());
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
