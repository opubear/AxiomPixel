#include "World.h"
#include "WorldStorageError.h"
#include "Runtime/Simulation/World/Environment/FieldResponse.h"
#include "Dev/Debug/Performance/FrameProfiler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>

namespace pixel {
World::World(uint32_t width, uint32_t height) : Buffers(width, height), Environment(Buffers) { InitializeBufferBudget(); }
World::World() : Environment(Buffers) { InitializeBufferBudget(); }

void World::InitializeBufferBudget() {
    Buffers.SetWriteReservation([this](uint64_t bytes, std::span<const CellCoord> chunks) { ReserveBufferWrite(bytes, chunks); },
        [this](uint64_t bytes) {
            if (Streamer && Streamer->RetainedBytes() + bytes > Streamer->Settings().BudgetBytes)
                throw WorldBudgetUnavailable{};
        });
}

void World::ReserveBufferWrite(uint64_t bytes, std::span<const CellCoord> protectedChunks) {
    if (!Streamer) return;
    const auto scratch = std::max(ScratchReservation, Environment.GetMemoryUsage().ReservedHeapBytes
        - std::as_const(Environment).GetWindSimulation().GetMemoryUsage().ReservedHeapBytes);
    const auto external = PublicationBytes + scratch;
    const bool reserved = Streamer->ReserveExternalBytes(external + bytes, GetSimulationBounds(), false, protectedChunks);
    Streamer->SetExternalBytes(external);
    if (!reserved) throw WorldBudgetUnavailable{};
}

CellQuery World::QueryCell(int64_t x, int64_t y) const {
    const auto cell = GetMap().TryGet({x, y});
    if (cell)
        return {ChunkResidency::Resident, cell};
    return {Streamer ? Streamer->GetState(ChunkOf({x, y})) : ChunkResidency::Missing, {}};
}

performance::MemorySnapshot World::GetMemoryUsage() const {
    auto total = performance::MeasureObject(*this);
    total.UsedHeapBytes += sizeof(WorldBuffer);
    total.ReservedHeapBytes += sizeof(WorldBuffer);
    total.AddEmbedded(GetMap().GetMemoryUsage());
    total.AddEmbedded(Environment.GetMemoryUsage());
    total.AddEmbedded(Buffers.RetainedStorage());
    if (Streamer)
        total.AddOwned(Streamer->GetMemoryUsage());
    total.UsedHeapBytes += PublicationBytes;
    total.ReservedHeapBytes += PublicationBytes;
    const auto base = GetMap().GetForceMemoryUsage();
    const auto wind = Environment.GetWindSimulation().GetMemoryUsage();
    return {total.ExcludingEmbedded(base).ExcludingEmbedded(wind), base, wind};
}

void World::RequireFinite() const {
    if (!GetMap().IsFinite())
        throw std::logic_error("unbounded world edits require an explicit region");
}

void World::Fill(CellState cell) {
    RequireFinite();
    GetMutableMap().Fill(GetMap().Bounds(), cell);
}

void World::SetForceDirection(int64_t x, int64_t y, ForceDirection direction) {
    auto cell = GetCell(x, y);
    cell.Force = direction;
    SetCell(x, y, cell);
}

void World::FillForceDirection(ForceDirection direction) {
    RequireFinite();
    GetMutableMap().FillDirection(GetMap().Bounds(), direction);
}

void World::AddBaseForce(int64_t x, int64_t y, ForceVector force) {
    const auto old = GetMap().GetForce({x, y});
    SetBaseForce(x, y, {old.X + force.X, old.Y + force.Y});
}

void World::FillBaseForce(ForceVector force) {
    RequireFinite();
    GetMutableMap().FillForce(GetMap().Bounds(), force);
}

void World::RandomizeForceDirections(uint32_t endRow, uint32_t seed) {
    RequireFinite();
    const auto bounds = GetMap().Bounds();
    if (endRow > bounds.Height)
        throw std::out_of_range("random field boundary is outside the world");
    constexpr std::array<ForceDirection, 8> directions{{{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
    std::mt19937 random(seed);
    std::uniform_int_distribution<size_t> choose(0, directions.size() - 1);
    GetMutableMap().PrepareWrite({0, 0, bounds.Width, endRow});
    for (uint32_t y = 0; y < endRow; ++y) {
        for (uint32_t x = 0; x < bounds.Width; ++x) {
            auto &chunk = *GetMutableMap().FindChunk(ChunkOf({x, y}));
            const auto direction = directions[choose(random)];
            const auto index = size_t(y % storage::ChunkEdge) * storage::ChunkEdge + x % storage::ChunkEdge;
            chunk.Data().Directions[index] = {int8_t(direction.X), int8_t(direction.Y)};
            chunk.Data().BaseForces[index] = FieldResponse::FromDirection(direction);
        }
    }
    GetMutableMap().Invalidate({0, 0, bounds.Width, endRow});
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
    const double time = std::as_const(Environment).GetWindSimulation().GetSimulationTime();
    Environment.GetWindSimulation().Configure(view, view.Height, settings);
    Environment.GetWindSimulation().SetSimulationTime(time);
    WindSettings = settings;
    WindBoundaryY = boundaryY;
    WindEnabled = enabled;
    GetMutableMap().Wake(GetSimulationBounds());
}

void World::SetWindBoundary(int64_t y) {
    ConfigureWindDomain(y, WindSettings);
}

void World::DisableWind() {
    WindEnabled = false;
    Environment.GetWindSimulation().Clear();
    GetMutableMap().Wake(GetSimulationBounds());
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
    Environment.RequireSourceMutation();
    Buffers.DiscardPublished();
    GetMutableMap().GenerateTerrain(surfaceRow);
    Environment.Reset();
    ScratchReservation = 0;
    WindEnabled = false;
    SimulationFocus.reset();
}

bool World::PlaceDirt(int64_t x, int64_t y) {
    return TryPlaceDirt(x, y) == CellEditResult::Applied;
}

CellEditResult World::TryPlaceDirt(int64_t x, int64_t y) {
    return TryPlaceMaterial({x, y}, CellTypeId::Dirt);
}

CellEditResult World::TryPlaceMaterial(CellCoord position, CellTypeId material) {
    if (!IsKnownCellType(material) || material == CellTypeId::Air)
        throw std::invalid_argument("placement requires a registered non-air material");
    const auto cell = TryGetCell(position.X, position.Y);
    if (!cell)
        return CellEditResult::NotReady;
    if (cell->Type != CellTypes::Empty)
        return CellEditResult::Blocked;
    GetMutableMap().SetMaterial(position, material);
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
    GetMutableMap().SetMaterial({x, y}, CellTypes::Empty);
    return CellEditResult::Applied;
}

CellEditResult World::TryRemoveMaterial(int64_t x, int64_t y) {
    const auto cell = TryGetCell(x, y);
    if (!cell)
        return CellEditResult::NotReady;
    if (!IsMineable(cell->Type))
        return CellEditResult::Blocked;
    GetMutableMap().SetMaterial({x, y}, CellTypes::Empty);
    return CellEditResult::Applied;
}

void World::PublishBuffers() {
    if (Environment.IsWaitingForBudget())
        throw std::logic_error("cannot publish a prepared environment step");
    Buffers.Publish(GetSimulationBounds(), GetWindBounds(), WindEnabled);
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
    destination.Update(GetMap(), bounds);
}

void World::DiscardStreaming() {
    Environment.RequireSourceMutation();
    Streamer.reset();
    Buffers.DiscardPublished();
    GetMutableMap().ResetInfinite();
    Environment.Reset();
    ScratchReservation = 0;
    VisibleBounds = {};
    SimulationFocus.reset();
    WindEnabled = false;
}

bool World::TryPrepareMaterialWrite(WorldRect bounds) {
    if (Environment.IsWaitingForBudget()) return false;
    try { GetMutableMap().PrepareWrite(bounds); }
    catch (const WorldBudgetUnavailable &) { return false; }
    return true;
}

bool World::TryPrepareBuffers() {
    if (Environment.IsWaitingForBudget()) return false;
    try { Buffers.Write(); }
    catch (const WorldBudgetUnavailable &) { return false; }
    return true;
}


void World::EnableStreaming(WorldStreamSettings settings, WorldRect initialBounds) {
    if (Streamer)
        Save();
    DiscardStreaming();
    SimulationBounds = initialBounds.Empty() ? WorldRect{0, 0, settings.WindowEdge, settings.WindowEdge} : initialBounds;
    Streamer = std::make_unique<WorldStreamer>([this]() -> const WorldMap & { return GetMap(); },
        [this]() -> WorldMap & { return GetMutableMap(); }, std::move(settings), [this] {
            return Buffers.RetainedStorage(false).ReservedBytes() + sizeof(WorldBuffer);
        });
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
    // A failed material step already advanced wind in the current domain.
    // Storage may make progress, but recentering/remapping must wait for commit.
    const bool deferred = Environment.IsWaitingForBudget();
    if (!deferred && !TryPrepareBuffers()) return;
    if (SimulationFocus && !deferred) {
        const auto cell = *SimulationFocus;
        const auto quarter = SimulationBounds.Width / 4;
        WorldRect centre{SimulationBounds.X + quarter, SimulationBounds.Y + quarter,
                         SimulationBounds.Width / 2, SimulationBounds.Height / 2};
        if (!centre.Contains(cell)) {
            auto next = SimulationBounds;
            next.X = FloorDivide(cell.X - next.Width / 2, storage::RegionEdge) * storage::RegionEdge;
            next.Y = FloorDivide(cell.Y - next.Height / 2, storage::RegionEdge) * storage::RegionEdge;
            if (next != SimulationBounds) {
                SimulationBounds = next;
                Environment.SimulationDomainChanged();
                GetMutableMap().Wake(SimulationBounds);
            }
        }
    }
    if (deferred) {
        try { Streamer->Poll(false); }
        catch (const WorldBudgetUnavailable &) {
            // A failed save may need its chunk reinserted. Keep that result
            // queued when the pending step could not allocate a writer yet.
        }
        return;
    }
    Streamer->Update(SimulationBounds, VisibleBounds);
    if (WindEnabled && !deferred) {
        const auto view = GetWindView();
        Environment.GetWindSimulation().MoveDomain(view, view.Height);
    }
}

void World::Save() {
    if (!Streamer) return;
    try {
        Buffers.Write();
        Streamer->Save(!Environment.IsWaitingForBudget());
    } catch (const WorldBudgetUnavailable &error) {
        throw WorldStorageError(error.what());
    }
}

void World::WaitForStreaming() {
    if (!Streamer) return;
    try {
        if (Environment.IsWaitingForBudget()) Streamer->WaitForSaves();
        else Streamer->Wait();
    } catch (const WorldBudgetUnavailable &) {
        // Completed I/O remains queued until a writable version fits again.
    }
}

void World::SetPublicationBytes(uint64_t bytes) {
    SetPublicationBytes(nullptr, bytes);
}

bool World::ReservePublicationBytes(const void *owner, uint64_t bytes) {
    const auto found = PublicationOwners.find(owner);
    const auto previous = found == PublicationOwners.end() ? 0 : found->second;
    const uint64_t total = PublicationBytes - previous + bytes;
    if (Streamer && bytes > previous && !Environment.IsWaitingForBudget() && !TryPrepareBuffers())
        return false;
    const auto scratch = std::max(ScratchReservation, Environment.GetMemoryUsage().ReservedHeapBytes - std::as_const(Environment).GetWindSimulation().GetMemoryUsage().ReservedHeapBytes);
    if (Streamer && !Streamer->ReserveExternalBytes(total + scratch, GetSimulationBounds(), !Environment.IsWaitingForBudget()))
        return false;
    PublicationOwners[owner] = bytes;
    PublicationBytes = total;
    return true;
}

void World::SetPublicationBytes(const void *owner, uint64_t bytes) {
    if (!ReservePublicationBytes(owner, bytes)) {
        if (Streamer)
            Streamer->WaitForSaves();
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
        const auto scratch = Environment.GetMemoryUsage().ReservedHeapBytes - std::as_const(Environment).GetWindSimulation().GetMemoryUsage().ReservedHeapBytes;
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

} // namespace pixel
