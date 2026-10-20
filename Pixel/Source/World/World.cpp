#include "World.h"
#include "Environment/FieldResponse.h"
#include "Performance/FrameProfiler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>

namespace pixel {
World::World(uint32_t width, uint32_t height) : Map(width, height), Environment(Map) {}
World::World() : Environment(Map) {}

CellQuery World::QueryCell(int64_t x, int64_t y) const {
    const auto cell = Map.TryGet({x, y});
    if (cell)
        return {ChunkResidency::Resident, cell};
    return {Streamer ? Streamer->GetState(ChunkOf({x, y})) : ChunkResidency::Missing, {}};
}

performance::MemorySnapshot World::GetMemoryUsage() const {
    auto total = performance::MeasureObject(*this);
    total.AddEmbedded(Map.GetMemoryUsage());
    total.AddEmbedded(Environment.GetMemoryUsage());
    if (Streamer)
        total.AddOwned(Streamer->GetMemoryUsage());
    total.UsedHeapBytes += PublicationBytes;
    total.ReservedHeapBytes += PublicationBytes;
    const auto base = Map.GetForceMemoryUsage();
    const auto wind = Environment.GetWindSimulation().GetMemoryUsage();
    return {total.ExcludingEmbedded(base).ExcludingEmbedded(wind), base, wind};
}

void World::RequireFinite() const {
    if (!Map.IsFinite())
        throw std::logic_error("unbounded world edits require an explicit region");
}

void World::Fill(CellState cell) {
    RequireFinite();
    Map.Fill(Map.Bounds(), cell);
}

void World::SetForceDirection(int64_t x, int64_t y, ForceDirection direction) {
    auto cell = GetCell(x, y);
    cell.Force = direction;
    SetCell(x, y, cell);
}

void World::FillForceDirection(ForceDirection direction) {
    RequireFinite();
    Map.FillDirection(Map.Bounds(), direction);
}

void World::AddBaseForce(int64_t x, int64_t y, ForceVector force) {
    const auto old = Map.GetForce({x, y});
    SetBaseForce(x, y, {old.X + force.X, old.Y + force.Y});
}

void World::FillBaseForce(ForceVector force) {
    RequireFinite();
    Map.FillForce(Map.Bounds(), force);
}

void World::RandomizeForceDirections(uint32_t endRow, uint32_t seed) {
    RequireFinite();
    const auto bounds = Map.Bounds();
    if (endRow > bounds.Height)
        throw std::out_of_range("random field boundary is outside the world");
    constexpr std::array<ForceDirection, 8> directions{{{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
    std::mt19937 random(seed);
    std::uniform_int_distribution<size_t> choose(0, directions.size() - 1);
    for (uint32_t y = 0; y < endRow; ++y) {
        for (uint32_t x = 0; x < bounds.Width; ++x) {
            auto &chunk = *Map.FindChunk(ChunkOf({x, y}));
            const auto direction = directions[choose(random)];
            const auto index = size_t(y % storage::ChunkEdge) * storage::ChunkEdge + x % storage::ChunkEdge;
            chunk.Data().Directions[index] = {int8_t(direction.X), int8_t(direction.Y)};
            chunk.Data().BaseForces[index] = FieldResponse::FromDirection(direction);
        }
    }
    Map.Invalidate({0, 0, bounds.Width, endRow});
}

void World::ConfigureWind(uint32_t endRow, const pixel::WindSettings &settings) {
    WindStrategySettings strategy;
    strategy.Kind = WindStrategyKind::Coarse;
    strategy.Coarse = settings;
    ConfigureWindStrategy(endRow, strategy);
}

void World::ConfigurePixelWind(uint32_t endRow, const PixelWindSettings &settings) {
    WindStrategySettings strategy;
    strategy.Kind = WindStrategyKind::Reference;
    strategy.Reference = settings;
    ConfigureWindStrategy(endRow, strategy);
}

void World::ConfigureWindStrategy(uint32_t endRow, const WindStrategySettings &settings) {
    const auto view = ReadState();
    if (endRow > view.Height)
        throw std::out_of_range("wind boundary is outside the simulation window");
    ConfigureWindDomain(view.Origin.Y + endRow, settings, endRow != 0);
}

void World::ConfigureWindDomain(int64_t boundaryY, const WindStrategySettings &settings, bool enabled) {
    auto view = ReadState();
    view.Height = !enabled || boundaryY <= view.Origin.Y ? 0 : boundaryY >= view.Origin.Y + view.Height ? view.Height
                                                                                                        : uint32_t(boundaryY - view.Origin.Y);
    Environment.GetWindSimulation().Configure(view, view.Height, settings);
    Environment.GetWindSimulation().SetSimulationTime(Environment.GetSimulationTime());
    WindSettings = settings;
    WindBoundaryY = boundaryY;
    WindEnabled = enabled;
    Map.Wake(GetSimulationBounds());
}

void World::SetWindBoundary(int64_t y) {
    ConfigureWindDomain(y, WindSettings);
}

void World::DisableWind() {
    WindEnabled = false;
    Environment.GetWindSimulation().Clear();
    Map.Wake(GetSimulationBounds());
}

WorldRect World::GetWindBounds() const {
    auto bounds = GetSimulationBounds();
    bounds.Height = !WindEnabled || WindBoundaryY <= bounds.Y ? 0 : WindBoundaryY >= bounds.Y + bounds.Height ? bounds.Height
                                                                                                              : uint32_t(WindBoundaryY - bounds.Y);
    return bounds;
}

WindVelocity World::GetFieldVelocity(int64_t x, int64_t y) const {
    return FieldResponse::MovementVelocity(GetForce(x, y));
}

void World::GenerateTerrain(uint32_t surfaceRow) {
    RequireFinite();
    Map.GenerateTerrain(surfaceRow);
    Environment = pixel::Environment(Map);
    ScratchReservation = 0;
    WindEnabled = false;
    Player.reset();
    PlayerHorizontal = 0;
    PlayerJump = false;
}

bool World::PlaceDirt(int64_t x, int64_t y) {
    return TryPlaceDirt(x, y) == CellEditResult::Applied;
}

CellEditResult World::TryPlaceDirt(int64_t x, int64_t y) {
    const auto cell = TryGetCell(x, y);
    if (!cell)
        return CellEditResult::NotReady;
    if ((Player && Player->Occupies(x, y)) || cell->Type != CellTypes::Empty)
        return CellEditResult::Blocked;
    Map.SetMaterial({x, y}, CellTypes::Dirt);
    return CellEditResult::Applied;
}

bool World::RemoveDirt(int64_t x, int64_t y) {
    return TryRemoveDirt(x, y) == CellEditResult::Applied;
}

CellEditResult World::TryRemoveDirt(int64_t x, int64_t y) {
    const auto cell = TryGetCell(x, y);
    if (!cell)
        return CellEditResult::NotReady;
    if (cell->Type != CellTypes::Dirt)
        return CellEditResult::Blocked;
    Map.SetMaterial({x, y}, CellTypes::Empty);
    return CellEditResult::Applied;
}

CellEditResult World::TryRemoveMaterial(int64_t x, int64_t y) {
    const auto cell = TryGetCell(x, y);
    if (!cell)
        return CellEditResult::NotReady;
    if (!IsMineable(cell->Type))
        return CellEditResult::Blocked;
    Map.SetMaterial({x, y}, CellTypes::Empty);
    return CellEditResult::Applied;
}

void World::PublishState(WorldSnapshot &destination) const {
    const auto view = destination.Read();
    const auto bounds = GetSimulationBounds();
    if ((view.Width || view.Height) && (view.Width != bounds.Width || view.Height != bounds.Height))
        throw std::invalid_argument("publication destination dimensions do not match");
    PublishSnapshot(bounds, destination);
}

void World::PublishSnapshot(WorldRect bounds, WorldSnapshot &destination) const {
    PIXEL_PROFILE_SCOPE(StateCopy);
    destination.Update(Map, bounds);
}

void World::RestorePlayer(WorldPosition position) {
    position.Normalize();
    Player = Character{};
    Player->OriginChunk = position.Chunk;
    Player->X = position.Local[0];
    Player->Y = position.Local[1];
    PlayerHorizontal = 0;
    PlayerJump = false;
    UpdateStreaming();
}

void World::SpawnPlayer(double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y) || (Map.IsFinite() && (x < 0 || y < 0 || x + Character::Width > Map.Bounds().Width || y + Character::Height > Map.Bounds().Height)))
        throw std::invalid_argument("player bounds must fit inside the world");
    RestorePlayer(WorldPosition::FromAbsolute(x, y));
}

void World::SetPlayerInput(int horizontal, bool jump) {
    PlayerHorizontal = std::clamp(horizontal, -1, 1);
    PlayerJump = jump;
}

void World::DiscardStreaming() {
    Streamer.reset();
    Map.ResetInfinite();
    Environment = pixel::Environment(Map);
    ScratchReservation = 0;
    VisibleBounds = {};
    Player.reset();
    WindEnabled = false;
}

void World::EnableStreaming(WorldStreamSettings settings, WorldRect initialBounds) {
    if (Streamer)
        Save();
    DiscardStreaming();
    SimulationBounds = initialBounds.Empty() ? WorldRect{0, 0, settings.WindowEdge, settings.WindowEdge} : initialBounds;
    Streamer = std::make_unique<WorldStreamer>(Map, std::move(settings));
    Streamer->SetExternalBytes(PublicationBytes);
    Streamer->LoadNow(SimulationBounds);
}

void World::RequestVisibleRegion(WorldRect bounds) {
    VisibleBounds = bounds;
    UpdateStreaming();
}

void World::UpdateStreaming() {
    if (!Streamer)
        return;
    if (Player) {
        const auto cell = Player->GetWorldPosition().Cell();
        const auto quarter = SimulationBounds.Width / 4;
        WorldRect centre{SimulationBounds.X + quarter, SimulationBounds.Y + quarter,
                         SimulationBounds.Width / 2, SimulationBounds.Height / 2};
        if (!centre.Contains(cell)) {
            SimulationBounds.X = FloorDivide(cell.X - SimulationBounds.Width / 2, storage::RegionEdge) * storage::RegionEdge;
            SimulationBounds.Y = FloorDivide(cell.Y - SimulationBounds.Height / 2, storage::RegionEdge) * storage::RegionEdge;
            Map.Wake(SimulationBounds);
        }
    }
    Streamer->Update(SimulationBounds, VisibleBounds);
    if (WindEnabled) {
        const auto view = GetWindView();
        Environment.GetWindSimulation().MoveDomain(view, view.Height);
    }
}

void World::Save() {
    if (Streamer)
        Streamer->Save();
}

void World::WaitForStreaming() {
    if (Streamer)
        Streamer->Wait();
}

void World::SetPublicationBytes(uint64_t bytes) {
    SetPublicationBytes(nullptr, bytes);
}

bool World::ReservePublicationBytes(const void *owner, uint64_t bytes) {
    const auto found = PublicationOwners.find(owner);
    const auto previous = found == PublicationOwners.end() ? 0 : found->second;
    const uint64_t total = PublicationBytes - previous + bytes;
    const auto scratch = std::max(ScratchReservation, Environment.GetMemoryUsage().ReservedHeapBytes - Environment.GetWindSimulation().GetMemoryUsage().ReservedHeapBytes);
    if (Streamer && !Streamer->ReserveExternalBytes(total + scratch, GetSimulationBounds()))
        return false;
    PublicationOwners[owner] = bytes;
    PublicationBytes = total;
    return true;
}

void World::SetPublicationBytes(const void *owner, uint64_t bytes) {
    if (!ReservePublicationBytes(owner, bytes)) {
        if (Streamer)
            Streamer->Wait();
        if (!ReservePublicationBytes(owner, bytes))
            throw std::runtime_error("world budget cannot fit publication storage");
    }
}

void World::ReleasePublication(const void *owner) {
    const auto found = PublicationOwners.find(owner);
    if (found == PublicationOwners.end())
        return;
    PublicationBytes -= found->second;
    PublicationOwners.erase(found);
    if (Streamer) {
        const auto scratch = Environment.GetMemoryUsage().ReservedHeapBytes - Environment.GetWindSimulation().GetMemoryUsage().ReservedHeapBytes;
        Streamer->SetExternalBytes(PublicationBytes + scratch);
    }
}

void World::ReserveSimulationBytes(uint64_t bytes) {
    // During a material step the read state is pinned. Completed loads are
    // accepted only at the next frame/fixed-step boundary, never mid-proposal.
    if (Streamer && !Streamer->ReserveExternalBytes(PublicationBytes + bytes, GetSimulationBounds(), false)) {
        Streamer->WaitForSaves();
        if (!Streamer->ReserveExternalBytes(PublicationBytes + bytes, GetSimulationBounds(), false))
            throw WorldBudgetUnavailable{};
    }
    ScratchReservation = bytes;
}

void World::Tick(float duration) {
    UpdateStreaming();
    Environment.Tick(duration, *this);
    if (Player && !Environment.IsWaitingForBudget()) {
        PIXEL_PROFILE_SCOPE(Character);
        Player->Tick(duration, ReadState(), PlayerHorizontal, PlayerJump);
    }
    PlayerJump = false;
}
} // namespace pixel
