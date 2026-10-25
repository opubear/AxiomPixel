#include "ObservationProjector.h"
#include "Runtime/Gameplay/Actions/ActionValues.h"
#include "Runtime/Gameplay/FormulaBridge/FormulaBridge.h"
#include "Runtime/Simulation/World/World.h"
#include "Runtime/Gameplay/Actors/Actors.h"
#include <algorithm>

namespace pixel {
struct ObservationProjector::Entry {
    api::ObservationSnapshot Snapshot;
    std::array<uint64_t, 4> RegionVersions{};
    uint64_t MapIdentity = 0, WindVersion = 0;
};

namespace {
WorldRect WorldBounds(api::Rect rect) { return {rect.Origin.X, rect.Origin.Y, rect.Width, rect.Height}; }
api::Rect PublicBounds(WorldRect rect) { return {{rect.X, rect.Y}, rect.Width, rect.Height}; }

api::ObservationStatus ValidateScope(api::Rect scope) {
    if (!scope.Width || !scope.Height) return api::ObservationStatus::InvalidArgument;
    if (scope.Width > ObservationProjector::MaximumEdge || scope.Height > ObservationProjector::MaximumEdge)
        return api::ObservationStatus::LimitExceeded;
    // Same supported chunk range as actions, with room for bin/region alignment.
    constexpr int64_t limit = ((int64_t{1} << 49) - 2) * storage::ChunkEdge;
    if (scope.Origin.X <= -limit || scope.Origin.Y <= -limit
        || scope.Origin.X >= limit - scope.Width || scope.Origin.Y >= limit - scope.Height)
        return api::ObservationStatus::InvalidArgument;
    return api::ObservationStatus::Available;
}

api::WindSampling WindMetadata(const World &world) {
    api::WindSampling result;
    const auto &settings = world.GetWindSettings();
    result.Backend = api::WindKind(settings.Kind);
    result.Enabled = world.IsWindEnabled();
    const auto bounds = world.GetWindBounds();
    result.Domain = PublicBounds(bounds);
    switch (settings.Kind) {
    case WindStrategyKind::Coarse: case WindStrategyKind::Hybrid: {
        const auto &coarse = settings.Coarse;
        if (result.Enabled && !bounds.Empty())
            result.SolverSpacingCells = {double(bounds.Width) / coarse.Columns, double(bounds.Height) / coarse.Rows};
        result.LocalObstacleCorrection = settings.Kind == WindStrategyKind::Hybrid;
        break;
    }
    case WindStrategyKind::Reference: case WindStrategyKind::Lbm:
        result.SolverSpacingCells = {1., 1.};
        break;
    case WindStrategyKind::Adaptive:
        result.AdaptiveMaximumEdgeCells = std::clamp(settings.Adaptive.BlockSize, 1u, 32u);
        break;
    case WindStrategyKind::Procedural: result.LocalObstacleCorrection = true; break;
    case WindStrategyKind::Empty: break;
    }
    return result;
}

void SampleAir(api::ObservedCell &sample, CellCoord cell, const World &world, const api::WindSampling &metadata) {
    sample.AirVelocity.reset();
    if (!sample.MaterialId) { sample.AirKind = api::AirSampleKind::NotResident; return; }
    if (*sample.MaterialId != uint32_t(CellTypeId::Air)) { sample.AirKind = api::AirSampleKind::NotApplicable; return; }
    sample.AirVelocity = {0, 0};
    // These reasons have explicit precedence; an out-of-domain sample remains
    // known zero even if a numerical backend offers periodic test boundaries.
    if (!metadata.Enabled) sample.AirKind = api::AirSampleKind::Disabled;
    else if (!WorldBounds(metadata.Domain).Contains(cell)) sample.AirKind = api::AirSampleKind::OutsideDomain;
    else if (metadata.Backend == api::WindKind::Empty) sample.AirKind = api::AirSampleKind::EmptyBackend;
    else {
        sample.AirKind = api::AirSampleKind::Sampled;
        const auto wind = world.SampleWind(cell);
        sample.AirVelocity = {wind.X, wind.Y};
    }
}

void UpdateObjects(api::ObservationSnapshot &snapshot, const Actors &actors, const FormulaBridge &formulas) {
    snapshot.Player.reset();
    if (const auto player = actors.PlayerSnapshot()) {
        const auto position = player->GetWorldPosition();
        const auto local = position.RelativeTo({snapshot.Scope.Origin.X, snapshot.Scope.Origin.Y});
        if (local[0] < snapshot.Scope.Width && local[1] < snapshot.Scope.Height
            && local[0] + Body::Width > 0 && local[1] + Body::Height > 0)
            snapshot.Player = api::PlayerObservation{ActionPosition(position), {Body::Width, Body::Height}, player->Grounded, player->Id.Value};
    }
    const auto stats = formulas.Stats();
    snapshot.Programs = {formulas.AppliedProgram(), uint32_t(stats.Roots), uint32_t(stats.Projectiles), uint32_t(stats.Pending)};
    snapshot.Projectiles.clear();
    for (const auto &bolt : formulas.Projectiles()) {
        const auto position = ActionPosition(bolt.Position);
        if (WorldBounds(snapshot.Scope).Contains({position.AnchorCell.X, position.AnchorCell.Y}))
            snapshot.Projectiles.push_back({bolt.CastId, bolt.ProjectileId, {bolt.ResourceId, bolt.Version}, position,
                {bolt.Velocity.X, bolt.Velocity.Y}, bolt.Radius});
    }
    snapshot.ObjectSampledAtRevision = snapshot.Stamp.Revision;
}

void UpdateGameplay(api::ObservationSnapshot &snapshot, const api::GameplayObservation &gameplay) {
    if (gameplay.Stamp != snapshot.Stamp) throw std::logic_error("domain observation has a different commit");
    auto &result = snapshot.Gameplay;
    result = {};
    result.Stamp = snapshot.Stamp;
    const auto bounds = WorldBounds(snapshot.Scope);
    const auto inside = [&](api::Cell cell) { return bounds.Contains({cell.X, cell.Y}); };
    for (const auto &inventory : gameplay.Inventories)
        if (inside(inventory.Location.AnchorCell)) result.Inventories.push_back(inventory);
    for (const auto &device : gameplay.Devices)
        if (inside(device.Anchor)) result.Devices.push_back(device);
    for (const auto &process : gameplay.Processes)
        if (inside(process.Location.AnchorCell)) result.Processes.push_back(process);
    for (const auto &source : gameplay.FieldSources)
        if (inside(source.Center.AnchorCell)) result.FieldSources.push_back(source);
}
} // namespace

ObservationProjector::ObservationProjector() = default;
ObservationProjector::~ObservationProjector() = default;

api::ObservationResult ObservationProjector::Capture(size_t slot, api::Rect scope, const World &world, const Actors &actors,
    const FormulaBridge &formulas, const api::GameplayObservation &gameplay, const api::CommitStamp &stamp, bool paused, bool ready, bool pending,
    uint64_t factHighWatermark) {
    const auto validation = ValidateScope(scope);
    if (validation != api::ObservationStatus::Available) return {validation};
    if (!ready) return {api::ObservationStatus::SessionUnavailable};
    auto &entry = Entries.at(slot);
    if (pending) {
        if (!entry || entry->Snapshot.Scope != scope || entry->Snapshot.Stamp.WorldEpoch != stamp.WorldEpoch)
            return {api::ObservationStatus::NotSampled};
    } else {
        if (!entry || entry->Snapshot.Scope != scope) {
            // Release before replacement: no second resident cache at the peak.
            entry.reset();
            entry = std::make_unique<Entry>();
            entry->Snapshot.Scope = scope;
            entry->Snapshot.Cells.resize(size_t(scope.Width) * scope.Height);
            entry->Snapshot.Projectiles.reserve(FormulaBridge::MaximumEffects);
        }
        Update(*entry, world, actors, formulas, gameplay, stamp, paused, factHighWatermark);
    }
    auto result = api::ObservationResult{api::ObservationStatus::Available, entry->Snapshot};
    Counters.ExportedBytes += result.Snapshot->RetainedBytes();
    return result;
}

void ObservationProjector::Update(Entry &entry, const World &world, const Actors &actors, const FormulaBridge &formulas,
    const api::GameplayObservation &gameplay, const api::CommitStamp &stamp, bool paused, uint64_t factHighWatermark) {
    if (world.GetEnvironment().IsWaitingForBudget()) throw std::logic_error("observation during pending step");
    auto &snapshot = entry.Snapshot;
    if (snapshot.Stamp == stamp) return;
    const bool changedWorld = entry.MapIdentity != world.GetMap().GetIdentity();
    const auto windVersion = world.GetEnvironment().GetWindSimulation().GetRevision();
    const bool changedWind = changedWorld || entry.WindVersion != windVersion;
    snapshot.Wind = WindMetadata(world);
    const auto bounds = WorldBounds(snapshot.Scope);
    size_t regionIndex = 0;
    const auto first = RegionOf({bounds.X, bounds.Y});
    for (auto ry = first.Y * storage::RegionEdge; ry < bounds.Y + bounds.Height; ry += storage::RegionEdge) {
        for (auto rx = first.X * storage::RegionEdge; rx < bounds.X + bounds.Width; rx += storage::RegionEdge) {
            const auto version = world.GetMap().RegionVersion({rx, ry});
            auto &previous = entry.RegionVersions.at(regionIndex++);
            const bool changedMaterial = changedWorld || previous != version;
            previous = version;
            if (!changedMaterial && !changedWind) continue;
            for (auto y = std::max(ry, bounds.Y); y < std::min(ry + storage::RegionEdge, bounds.Y + bounds.Height); ++y) {
                for (auto x = std::max(rx, bounds.X); x < std::min(rx + storage::RegionEdge, bounds.X + bounds.Width); ++x) {
                    auto &sample = snapshot.Cells[size_t(y - bounds.Y) * bounds.Width + size_t(x - bounds.X)];
                    if (changedMaterial) {
                        const auto cell = world.TryGetCell(x, y);
                        if (sample.MaterialId) --snapshot.ResidentCellCount;
                        sample.MaterialId = cell ? std::optional(uint32_t(cell->Type)) : std::nullopt;
                        if (sample.MaterialId) ++snapshot.ResidentCellCount;
                        sample.MaterialSampledAtRevision = stamp.Revision;
                        ++Counters.MaterialCellsSampled;
                    }
                    SampleAir(sample, {x, y}, world, snapshot.Wind);
                    sample.WindSampledAtRevision = stamp.Revision;
                    ++Counters.WindCellsSampled;
                    Counters.CellBytesWritten += sizeof(api::ObservedCell);
                }
            }
        }
    }
    entry.MapIdentity = world.GetMap().GetIdentity();
    entry.WindVersion = windVersion;
    snapshot.Stamp = stamp;
    snapshot.FactHighWatermark = factHighWatermark;
    snapshot.Paused = paused;
    UpdateObjects(snapshot, actors, formulas);
    UpdateGameplay(snapshot, gameplay);
    if (snapshot.RetainedBytes() > MaximumSnapshotBytes)
        throw std::logic_error("observation exceeded its fixed payload budget");
}

void ObservationProjector::Publish(const World &world, const Actors &actors, const FormulaBridge &formulas,
    const api::GameplayObservation &gameplay, const api::CommitStamp &stamp, bool paused, uint64_t factHighWatermark) {
    for (auto &entry : Entries)
        if (entry) Update(*entry, world, actors, formulas, gameplay, stamp, paused, factHighWatermark);
}

void ObservationProjector::Close(size_t slot) { Entries.at(slot).reset(); }
void ObservationProjector::Reset() { for (auto &entry : Entries) entry.reset(); }

api::ObservationMetrics ObservationProjector::Metrics() const {
    auto result = Counters;
    result.RetainedBytes = sizeof(*this);
    result.MaximumRetainedBytes = sizeof(*this) + MaximumScopes * (MaximumSnapshotBytes + sizeof(Entry));
    for (const auto &entry : Entries) {
        if (!entry) continue;
        ++result.ActiveScopes;
        result.RetainedBytes += sizeof(Entry) - sizeof(api::ObservationSnapshot) + entry->Snapshot.RetainedBytes();
    }
    return result;
}
} // namespace pixel
