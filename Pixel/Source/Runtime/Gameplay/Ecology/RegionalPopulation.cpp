#include "RegionalPopulation.h"
#include "Runtime/Simulation/World/World.h"
#include <algorithm>
#include <cmath>

namespace pixel {
namespace {
constexpr int SearchRadius = 16;
constexpr int64_t SceneAnchor = 511;

struct Candidate {
    PopulationSite Site;
    std::array<float, 2> Size;
    uint32_t AboveGround;
    BiomeSpawnWeights Weights;
};
template<class Visitor>
void VisitCandidates(int64_t anchor, const MonsterCatalog *catalog, bool residents, Visitor visit) {
    if (residents)
        for (const int offset : {-10, 20})
            visit(Candidate{{anchor + offset, ActorKind::Resident, {}},
                {Body::WidthCells, Body::HeightCells}, 0, ResidentBiomeWeights});
    if (catalog)
        for (const auto &spawn : catalog->Spawns) {
            const auto &definition = catalog->Require(spawn.Id);
            visit(Candidate{{anchor + spawn.OffsetCells, ActorKind::Monster, spawn.Id},
                definition.BodySizeCells, spawn.HeightAboveGroundCells, definition.BiomeWeights});
        }
}

bool ResidentArea(const World &world, WorldRect area) {
    const auto active = world.GetSimulationBounds();
    const CellCoord end{area.X + area.Width - 1, area.Y + area.Height - 1};
    if (!active.Contains({area.X, area.Y}) || !active.Contains(end)) return false;
    const auto first = ChunkOf({area.X, area.Y}), last = ChunkOf(end);
    for (auto y = first.Y; y <= last.Y; ++y)
        for (auto x = first.X; x <= last.X; ++x)
            if (!world.GetMap().FindChunk({x, y})) return false;
    return true;
}

WorldRect BodyBounds(const ActorRecord &actor) {
    const auto position = actor.Body.GetWorldPosition();
    const auto first = position.Cell();
    const auto offset = position.RelativeCellsTo(first);
    return {first.X, first.Y,
        uint32_t(std::ceil(offset[0] + CentimetersToCells(actor.Body.Size[0]))),
        uint32_t(std::ceil(offset[1] + CentimetersToCells(actor.Body.Size[1])))};
}

enum class PlacementStatus { Waiting, Empty, Ready };
enum class SearchResidency { Complete, Available };
struct Placement {
    PlacementStatus Status = PlacementStatus::Waiting;
    WorldPosition Position;
};

Placement FindSurfaceSpawn(const World &world, const Actors &actors,
    const IWorldGenerator *generator, const Candidate &candidate, SearchResidency residency) {
    const auto &[site, size, aboveGround, weights] = candidate;
    const auto active = world.GetSimulationBounds();
    const auto width = uint32_t(std::ceil(size[0])), height = uint32_t(std::ceil(size[1]));
    const auto left = site.AnchorX - SearchRadius;
    const auto right = site.AnchorX + SearchRadius + width - 1;
    if (residency == SearchResidency::Complete && (left < active.X || right >= active.X + active.Width)) return {};
    // The full search, body and support footprint must be available before a
    // candidate can be consumed, regardless of entry direction or chunk size.
    try {
        ValidateGenerationPoint({left, 0});
        ValidateGenerationPoint({right, 0});
    } catch (const std::out_of_range &) { return {}; }
    const auto roll = BiomeSpawnRoll(world.GetGenerationSettings().Seed,
        site.Kind == ActorKind::Resident ? "resident" : site.Species, site.AnchorX);
    std::array<int64_t, SearchRadius * 2 + 1> surfaces{};
    bool suitable = false;
    int64_t top = INT64_MAX, bottom = INT64_MIN;
    for (int offset = -SearchRadius; offset <= SearchRadius; ++offset) {
        const auto x = site.AnchorX + offset;
        suitable |= AcceptBiomeSpawn(world.SampleBiome({x, 0}), weights, roll);
        const auto surface = generator ? generator->SurfaceHeight(x) : world.GetGenerationSettings().SurfaceY;
        surfaces[size_t(offset + SearchRadius)] = surface;
        top = std::min(top, surface - 8 - height - aboveGround);
        bottom = std::max(bottom, surface + 8);
    }
    if (!suitable) return {PlacementStatus::Empty};
    if (residency == SearchResidency::Complete
        && !ResidentArea(world, {left, top, uint32_t(right - left + 1), uint32_t(bottom - top + 1)})) return {};

    const auto terrain = world.ReadState();
    bool occupiedSite = false;
    for (int index = 0; index <= SearchRadius * 2; ++index) {
        const int offset = index % 2 ? -(index + 1) / 2 : index / 2;
        const auto x = site.AnchorX + offset;
        if (!AcceptBiomeSpawn(world.SampleBiome({x, 0}), weights, roll)) continue;
        for (int dy = -8; dy <= 8; ++dy) {
            const auto y = surfaces[size_t(offset + SearchRadius)] + dy;
            Body body;
            body.Size = {CellsToCentimeters(size[0]), CellsToCentimeters(size[1])};
            const auto position = WorldPosition::FromCell({x, y}, {0, -double(size[1]) - aboveGround});
            body.RestoreState({position});
            if (!body.CanOccupy(terrain)) continue;
            bool supported = true, occupied = false;
            for (uint32_t column = 0; column < width; ++column) {
                const auto support = world.TryGetCell(x + column, y);
                supported &= support && BlocksCharacter(support->Type);
            }
            if (!supported) continue;
            const auto first = position.Cell();
            for (uint32_t row = 0; row <= height; ++row)
                for (uint32_t column = 0; column < width; ++column)
                    occupied |= actors.Occupies({first.X + column, first.Y + row});
            if (!occupied) return {PlacementStatus::Ready, position};
            occupiedSite = true;
        }
    }
    // Temporary actor occupancy must not turn a valid candidate into a tombstone.
    return {occupiedSite ? PlacementStatus::Waiting : PlacementStatus::Empty};
}

bool Matches(const PopulationSite &site, const ActorRecord &actor) {
    return site.Kind == actor.Kind && (site.Kind == ActorKind::Resident
        || (actor.Monster && actor.Monster->Definition == site.Species));
}
} // namespace

void RegionalPopulation::SynchronizeActivity(const World &world, Actors &actors, std::span<const ActorId> pinned) {
    for (const auto &actor : actors.CaptureState().Records) {
        if (!OwnedActors.contains(actor.Id.Value) || actor.Formula
            || std::find(pinned.begin(), pinned.end(), actor.Id) != pinned.end()
            || world.GetSimulationBounds().Intersects(BodyBounds(actor))) continue;
        Dormant.emplace(actor.Id.Value, actor);
        actors.Remove(*actors.Resolve(actor.Id));
    }
    for (auto it = Dormant.begin(); it != Dormant.end();) {
        if (actors.Count() == Actors::MaximumCount) break;
        if (!ResidentArea(world, BodyBounds(it->second))) { ++it; continue; }
        if (!actors.Restore(it->second)) break;
        it = Dormant.erase(it);
    }
}

void RegionalPopulation::Activate(const World &world, Actors &actors, const MonsterCatalog *catalog,
                                  std::span<const ActorId> pinned, bool residents) {
    const auto settings = world.GetGenerationSettings();
    if (settings.Kind != WorldGeneratorKind::Natural || settings.Version != 2) return;
    SynchronizeActivity(world, actors, pinned);
    if (Decisions.size() == MaximumSites || actors.Count() == Actors::MaximumCount) return;
    const auto bounds = world.GetSimulationBounds();
    const auto generator = CreateWorldGenerator(settings);
    // Catalog offsets may extend 1024 cells from their fixed 512-cell anchor.
    const auto first = FloorDivide(bounds.X - SceneAnchor - 1024, RegionWidth);
    const auto last = FloorDivide(bounds.X + bounds.Width - 1 - SceneAnchor + 1024, RegionWidth);
    for (auto region = first; region <= last; ++region)
        VisitCandidates(region * RegionWidth + SceneAnchor, catalog, residents, [&](const Candidate &candidate) {
            if (Decisions.size() == MaximumSites || actors.Count() == Actors::MaximumCount
                || Decisions.contains(candidate.Site)) return;
            const auto placement = FindSurfaceSpawn(world, actors, generator.get(), candidate, SearchResidency::Complete);
            if (placement.Status == PlacementStatus::Waiting) return;
            ActorId id;
            if (placement.Status == PlacementStatus::Ready) {
                const auto handle = candidate.Site.Kind == ActorKind::Resident
                    ? actors.Spawn(ActorKind::Resident, placement.Position)
                    : actors.SpawnMonster(candidate.Site.Species, placement.Position);
                if (!handle) return;
                id = actors.Require(*handle).Id;
                OwnedActors.insert(id.Value);
            }
            Decisions.emplace(candidate.Site, id);
        });
}

RegionalPopulationState RegionalPopulation::CaptureState() const {
    RegionalPopulationState state;
    for (const auto &[site, actor] : Decisions) state.Decisions.push_back({site, actor});
    for (const auto &[id, actor] : Dormant) state.Dormant.push_back(actor);
    return state;
}

void RegionalPopulation::RestoreState(const RegionalPopulationState &state, const Actors &actors,
                                     std::shared_ptr<const MonsterCatalog> catalog) {
    if (state.Version != 1 || state.Decisions.size() > MaximumSites || state.Dormant.size() > MaximumSites)
        throw std::invalid_argument("invalid regional population checkpoint");
    RegionalPopulation restored;
    const auto nextId = actors.CaptureState().NextId;
    for (const auto &entry : state.Decisions) {
        const auto &site = entry.Site;
        ValidateGenerationPoint({site.AnchorX, 0});
        if ((site.Kind != ActorKind::Resident && site.Kind != ActorKind::Monster)
            || (site.Kind == ActorKind::Resident ? !site.Species.empty() : site.Species.empty())
            || site.Species.size() > 32 || site.Species.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos
            || entry.Actor.Value >= nextId || !restored.Decisions.emplace(site, entry.Actor).second
            || (entry.Actor.Value && !restored.OwnedActors.insert(entry.Actor.Value).second))
            throw std::invalid_argument("invalid regional population candidate or identity");
    }
    Actors validator(std::move(catalog));
    for (const auto &actor : state.Dormant) {
        if (!restored.OwnedActors.contains(actor.Id.Value) || actors.Find(actor.Id) || actor.Formula
            || !restored.Dormant.emplace(actor.Id.Value, actor).second)
            throw std::invalid_argument("invalid dormant population identity");
        const auto handle = validator.Restore(actor);
        // Reconstruct content-defined body dimensions before activity queries.
        restored.Dormant.at(actor.Id.Value) = validator.Require(*handle);
        validator.Remove(*handle);
    }
    for (const auto &[site, id] : restored.Decisions) {
        const auto sleeping = restored.Dormant.find(id.Value);
        const auto active = actors.Find(id);
        if ((sleeping != restored.Dormant.end() && !Matches(site, sleeping->second))
            || (active && !Matches(site, *active)))
            throw std::invalid_argument("population candidate and actor disagree");
    }
    *this = std::move(restored);
}

void RegionalPopulation::ImportLegacyScene(const Actors &actors, const MonsterCatalog *catalog) {
    Clear();
    const auto saved = actors.CaptureState();
    // Old scenes were always anchored at x=511, even if the saved player has
    // since travelled elsewhere. Missing initial actors may already be dead.
    VisitCandidates(SceneAnchor, catalog, true, [&](const Candidate &candidate) {
        if (Decisions.contains(candidate.Site)) return;
        ActorId id;
        for (const auto &actor : saved.Records) {
            const auto origin = actor.Monster ? actor.Monster->Home : actor.Body.GetWorldPosition();
            if (Matches(candidate.Site, actor) && !OwnedActors.contains(actor.Id.Value)
                && std::abs(origin.Cell().X - candidate.Site.AnchorX) <= SearchRadius) {
                id = actor.Id;
                OwnedActors.insert(id.Value);
                break;
            }
        }
        Decisions.emplace(candidate.Site, id);
    });
}

void RegionalPopulation::Clear() { Decisions.clear(); Dormant.clear(); OwnedActors.clear(); }

void SpawnLegacyScenePopulation(const World &world, Actors &actors, const MonsterCatalog *catalog) {
    const auto generator = world.GetStreamer() ? CreateWorldGenerator(world.GetGenerationSettings()) : nullptr;
    VisitCandidates(actors.Player()->GetWorldPosition().Cell().X, catalog, false, [&](const Candidate &candidate) {
        // Legacy scenes are attempted once. Keep their original partial-window
        // search; only regional decisions require a complete search footprint.
        const auto placement = FindSurfaceSpawn(world, actors, generator.get(), candidate, SearchResidency::Available);
        if (placement.Status == PlacementStatus::Ready && !actors.SpawnMonster(candidate.Site.Species, placement.Position))
            throw std::logic_error("no capacity for configured monsters");
    });
}
} // namespace pixel
