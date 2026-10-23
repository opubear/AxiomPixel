#include "GameSession.h"
#include "Persistence/GameplaySaveData.h"
#include "World/WorldStorageError.h"
#include <algorithm>
#include <set>

namespace pixel {
GameplaySaveData GameSession::CaptureGameplayData() const {
    if (Simulation.HasPendingStep()) throw std::logic_error("capturing incomplete gameplay commit");
    return {Simulation.Tick(), Simulation.Revision(), PlayerBag, LastAim, Characters.CaptureState(),
            Inventory.CaptureState(), Machines.CaptureState(),
            GameWorld.GetEnvironment().GetFieldSources().CaptureState(), Formulas.CaptureState(), Garden};
}

void GameSession::RestoreGameplayData(const GameplaySaveData &data) {
    Characters.RestoreState(data.Actors);
    Inventory.RestoreState(data.Inventory);
    Machines.RestoreState(data.Devices);
    Garden = data.Garden;
    if (!Characters.Player() || !Inventory.Contains(data.PlayerBag))
        throw WorldStorageError("checkpoint missing player or player inventory");
    PlayerBag = data.PlayerBag;
    LastAim = data.LastAim;
    Formulas.BindPlayerInventory(Inventory, PlayerBag);
    Formulas.RestoreState(data.Programs);
    GameWorld.RestoreFieldSources(data.FieldSources);
    const auto processes = Formulas.InspectProcesses();
    std::set<uint64_t> containers{PlayerBag.Value};
    for (const auto &record : data.Devices.Records) {
        const auto &d = record.Value;
        if (!Inventory.Contains(record.FuelContainer) || !containers.insert(record.FuelContainer.Value).second)
            throw WorldStorageError("device inventory reference is missing or shared");
        const auto fuel = Inventory.Inspect(record.FuelContainer);
        if (fuel.Quantity != d.Fuel || fuel.Capacity != 64 || fuel.Stacks.size() > 1
            || (!fuel.Stacks.empty() && fuel.Stacks.front().Item != items::ItemId::Coal))
            throw WorldStorageError("device fuel and inventory disagree");
        if (d.Enabled && !Characters.Find(ActorId{d.Owner}))
            throw WorldStorageError("enabled device owner is missing");
        if (!d.Instance) continue;
        const auto found = std::find_if(processes.begin(), processes.end(), [&](const auto &p) {
            return p.State.Id == d.Instance && p.Carrier == ProcessCarrierKind::Device && p.Subject == d.Ref.Id;
        });
        if (found == processes.end()) throw WorldStorageError("device program instance is missing");
    }
    for (const auto &process : processes) {
        if (process.Carrier == ProcessCarrierKind::Device) {
            const auto *device = Machines.Find(process.Subject);
            if (!device || !device->Value.Enabled || device->Value.Instance != process.State.Id)
                throw WorldStorageError("program device carrier identity mismatch");
        }
        // A source may have expired before a waiting program observes that fact.
        // Existing handles, however, must never acquire another instance's effect.
        for (const auto &source : data.FieldSources.Sources) {
            if (std::find(process.ReferencedFieldHandles.begin(), process.ReferencedFieldHandles.end(), source.Handle.Id)
                    != process.ReferencedFieldHandles.end() && source.Owner != process.State.Id)
                throw WorldStorageError("program field source ownership mismatch");
            if (source.Owner == process.State.Id && source.Suspended != process.Suspended)
                throw WorldStorageError("program and field activity disagree");
        }
    }
}
} // namespace pixel
