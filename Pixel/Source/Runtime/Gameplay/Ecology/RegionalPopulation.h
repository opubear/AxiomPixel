#pragma once
#include "Runtime/Gameplay/Actors/Actors.h"
#include <map>
#include <set>
#include <span>

namespace pixel {
class World;

// A candidate keeps its identity when a content list is reordered. Resident
// sites have an empty Species; monster sites name the catalog definition.
struct PopulationSite {
    int64_t AnchorX = 0;
    ActorKind Kind = ActorKind::Resident;
    std::string Species;
    auto operator<=>(const PopulationSite &) const = default;
};
struct PopulationDecision {
    PopulationSite Site;
    // Zero means rejected. A missing nonzero identity is a death/removal
    // tombstone, never permission to generate a replacement.
    ActorId Actor;
};
struct RegionalPopulationState {
    uint32_t Version = 1;
    std::vector<PopulationDecision> Decisions;
    std::vector<ActorRecord> Dormant;
};

class RegionalPopulation {
  public:
    static constexpr int64_t RegionWidth = 512;
    static constexpr size_t MaximumSites = 16384;
    // Called only during scene initialization or a successful simulation step.
    // Existing objects wake before new candidates can consume active slots.
    void Activate(const World &world, Actors &actors, const MonsterCatalog *catalog,
                  std::span<const ActorId> pinned = {}, bool residents = true);
    RegionalPopulationState CaptureState() const;
    void RestoreState(const RegionalPopulationState &state, const Actors &actors,
                      std::shared_ptr<const MonsterCatalog> catalog);
    void ImportLegacyScene(const Actors &actors, const MonsterCatalog *catalog);
    void Clear();

  private:
    std::map<PopulationSite, ActorId> Decisions;
    std::map<uint64_t, ActorRecord> Dormant;
    std::set<uint64_t> OwnedActors;
    void SynchronizeActivity(const World &world, Actors &actors, std::span<const ActorId> pinned);
};

// Flat and Natural v1 keep their original one-time scene population.
void SpawnLegacyScenePopulation(const World &world, Actors &actors, const MonsterCatalog *catalog);
} // namespace pixel
