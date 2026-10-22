#include "GameSession.h"

namespace pixel {
api::CommitStamp GameSession::CurrentCommit() const {
    return {SaveDirectory.empty() ? "finite-world" : SaveDirectory.filename().string(),
            Epoch, Simulation.Tick(), Simulation.Revision()};
}

std::shared_ptr<CommittedWorldView> GameSession::CreateReadView() {
    auto &world = GameWorld;
    const auto lifetime = world.PublicationLifetime();
    auto view = std::shared_ptr<CommittedWorldView>(new CommittedWorldView,
        [&world, lifetime](CommittedWorldView *value) {
            if (!lifetime.expired()) world.ReleasePublication(value);
            delete value;
        });
    ReadViews.push_back(view);
    return view;
}

void GameSession::RefreshReadView(CommittedWorldView &view, WorldRect bounds) {
    if (bounds.X > INT64_MAX - bounds.Width || bounds.Y > INT64_MAX - bounds.Height
        || bounds.X < INT64_MIN + storage::RegionEdge || bounds.Y < INT64_MIN + storage::RegionEdge)
        throw std::invalid_argument("committed read bounds exceed coordinate range");
    if (bounds.Empty() || bounds.Width > 2048 || bounds.Height > 2048)
        throw std::invalid_argument("committed read bounds must be in [1,2048] cells");
    if (!Ready || Simulation.HasPendingStep()) return;
    const auto reserve = CommittedWorldView::CapacityFor(bounds);
    const auto previous = GameWorld.PublicationBytesFor(&view);
    // Resizing may temporarily retain both buffers. Reserve before allocating;
    // capacity pressure keeps the previous complete projection available.
    const auto peak = view.RequestedBounds == bounds ? reserve : reserve + previous;
    if (!GameWorld.ReservePublicationBytes(&view, peak)) {
        SynchronizeReadViews();
        return;
    }
    view.RequestedBounds = bounds;
    GameWorld.RequestVisibleRegion(bounds);
    SynchronizeReadViews();
    view.Update(GameWorld, Formulas, CurrentCommit());
    GameWorld.SetPublicationBytes(&view, std::max(reserve, view.ReservedBytes()));
}

void GameSession::PublishCommit() {
    if (Simulation.HasPendingStep()) throw std::logic_error("publication during pending step");
    for (auto it = ReadViews.begin(); it != ReadViews.end();) {
        if (auto view = it->lock()) {
            if (!view->RequestedBounds.Empty()) view->Update(GameWorld, Formulas, CurrentCommit());
            ++it;
        } else {
            it = ReadViews.erase(it);
        }
    }
    PublishedMapVersion = GameWorld.GetMap().GetVersion();
    PublishedWindVersion = GameWorld.GetEnvironment().GetWindSimulation().GetRevision();
}

void GameSession::CommitChange() {
    Simulation.CommitChange();
    PublishCommit();
}

void GameSession::SynchronizeReadViews() {
    if (Simulation.HasPendingStep()) return;
    if (PublishedMapVersion != GameWorld.GetMap().GetVersion()
        || PublishedWindVersion != GameWorld.GetEnvironment().GetWindSimulation().GetRevision())
        CommitChange();
}

void GameSession::ServiceStreaming() {
    GameWorld.UpdateStreaming();
    SynchronizeReadViews();
}
} // namespace pixel
