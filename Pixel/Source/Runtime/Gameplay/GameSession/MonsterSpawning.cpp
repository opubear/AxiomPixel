#include "GameSession.h"

namespace pixel {
void GameSession::SpawnSceneActors() {
    if (GameWorld.SampleBiome(GetPlayer()->GetWorldPosition().Cell()).Regional)
        ActivateRegionalPopulation();
    else
        SpawnLegacyScenePopulation(GameWorld, Characters, Monsters.get());
}

void GameSession::ActivateRegionalPopulation() {
    const auto settings = GameWorld.GetGenerationSettings();
    if (settings.Kind != WorldGeneratorKind::Natural || settings.Version != 2) return;
    // Referenced objects stay in Actors until their dependent domain can
    // participate in dormancy. This preserves carrier and owner identities.
    std::vector<ActorId> pinned;
    for (const auto &device : Machines.Snapshot()) pinned.push_back({device.Owner});
    for (const auto &process : Formulas.InspectProcesses()) {
        const auto &carrier = process.Configuration;
        if (carrier.Binding == ProcessBindingKind::Actor) pinned.push_back(carrier.AttachedActor);
        if (carrier.Kind == ProcessCarrierKind::Player) pinned.push_back({carrier.Subject});
    }
    for (const auto &discovery : Knowledge.Export().Discoveries) pinned.push_back({discovery.OwnerActor});
    Population.Activate(GameWorld, Characters, Monsters.get(), pinned, !Garden.Enabled);
}
} // namespace pixel
