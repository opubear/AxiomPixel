#include "GameSession.h"
#include "Persistence/GameplaySaveData.h"
#include "World/WorldStorageError.h"
#include <algorithm>
#include <set>

namespace pixel {
GameplaySaveData GameSession::CaptureGameplayData() const {
    if (Simulation.HasPendingStep()) throw std::logic_error("capturing incomplete gameplay commit");
    GameplaySaveData data{Simulation.Tick(), Simulation.Revision(), PlayerBag, LastAim, Characters.CaptureState(),
            Inventory.CaptureState(), Machines.CaptureState(),
            GameWorld.GetEnvironment().GetFieldSources().CaptureState(), Formulas.CaptureState(), Garden};
    data.Authority = Authority.CaptureState();
    data.Authority.NextIntervention = NextIntervention;
    data.Authority.Interventions = Interventions;
    data.Knowledge = Knowledge.Export();
    data.WorldWill = WillMemory;
    return data;
}

void GameSession::RestoreGameplayData(const GameplaySaveData &data, const std::string &worldId) {
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
    RestoreSocialData(data, worldId);
}

void GameSession::RestoreSocialData(const GameplaySaveData &data, const std::string &worldId) {
    Authority.RestoreState(data.Authority, WorldWillSession(), Machines.Snapshot());
    if (!data.Authority.NextIntervention || data.Authority.NextIntervention == UINT64_MAX)
        throw WorldStorageError("invalid intervention identity watermark");
    const auto processes = Formulas.InspectProcesses();
    std::set<uint64_t> interventions;
    for (const auto &entry : data.Authority.Interventions) {
        if (!entry.Id || entry.Id >= data.Authority.NextIntervention || !interventions.insert(entry.Id).second
            || entry.ExpiresAtTick <= data.Tick || entry.ExpiresAtTick - data.Tick > 600
            || !Authority.CanIntervene(WorldWillSession(), entry.Scope))
            throw WorldStorageError("invalid or unauthorized saved intervention");
        const auto count = std::count_if(processes.begin(), processes.end(), [&](const auto &process) {
            return process.Carrier == ProcessCarrierKind::Intervention && process.Subject == entry.Id;
        });
        if (count != 1) throw WorldStorageError("intervention program is missing or duplicated");
    }
    for (const auto &process : processes) {
        if (process.Carrier != ProcessCarrierKind::Intervention) continue;
        const auto entry = std::find_if(data.Authority.Interventions.begin(), data.Authority.Interventions.end(),
            [&](const auto &value) { return value.Id == process.Subject; });
        const auto &carrier = process.Configuration;
        const api::Rect scope{{carrier.Scope.X, carrier.Scope.Y}, carrier.Scope.Width, carrier.Scope.Height};
        if (entry == data.Authority.Interventions.end() || entry->Scope != scope
            || process.Credits < 0 || process.Credits > 64 || carrier.MaximumRadius != 16
            || carrier.ActivityRadius != 0 || carrier.Grants != formula::process::AllCapabilities
            || carrier.AngleDegrees != 0)
            throw WorldStorageError("intervention carrier and authority disagree");
        for (const auto &source : data.FieldSources.Sources) {
            if (source.Owner != process.State.Id) continue;
            const auto center = source.Spec.Center.RelativeTo({scope.Origin.X, scope.Origin.Y});
            const auto radius = source.Spec.Radius;
            if (radius > carrier.MaximumRadius || center[0] - radius < 0 || center[1] - radius < 0
                || center[0] + radius >= scope.Width || center[1] + radius >= scope.Height)
                throw WorldStorageError("saved intervention field escaped its scope");
        }
    }
    NextIntervention = data.Authority.NextIntervention;
    Interventions = data.Authority.Interventions;

    const auto &knowledge = data.Knowledge;
    if ((!knowledge.WorldId.empty() && knowledge.WorldId != worldId)
        || knowledge.LastRevision > data.Revision || knowledge.LastTick > data.Tick)
        throw WorldStorageError("knowledge belongs to another checkpoint");
    for (const auto &entry : knowledge.History)
        if (entry.Source.CommittedAt.Revision > data.Revision || entry.Source.CommittedAt.Tick > data.Tick)
            throw WorldStorageError("knowledge contains future history");
    for (const auto &discovery : knowledge.Discoveries)
        if (!Characters.Find(ActorId{discovery.OwnerActor})) throw WorldStorageError("discovery owner is missing");
    Knowledge.Restore(knowledge);
    Knowledge.BeginRuntime(Epoch);

    WillMemory = data.WorldWill;
    for (auto &prayer : WillMemory.Records) {
        const auto validStamp = [&](const api::CommitStamp &stamp) {
            return stamp.WorldId == worldId && stamp.Tick <= data.Tick && stamp.Revision <= data.Revision;
        };
        if (!validStamp(prayer.ObservedAt)
            || (prayer.State != api::DecisionState::Thinking && prayer.State != api::DecisionState::Ready
                && !validStamp(prayer.ResolvedAt)))
            throw WorldStorageError("prayer belongs to another checkpoint");
        if (prayer.State == api::DecisionState::Applied) {
            if (prayer.EvidenceSequence >= knowledge.NextSequence)
                throw WorldStorageError("prayer cites nonexistent history");
            const auto evidence = std::find_if(knowledge.History.begin(), knowledge.History.end(),
                [&](const auto &record) { return record.Sequence == prayer.EvidenceSequence; });
            if (evidence != knowledge.History.end()) {
                const auto *activation = std::get_if<api::ProgramActivated>(&evidence->Source.Payload);
                const auto *request = std::get_if<api::RequestKey>(&evidence->Source.Cause);
                if (!activation || !request || *request != *prayer.Action || activation->ActivationId != prayer.InstanceId)
                    throw WorldStorageError("prayer and activation evidence disagree");
            }
        }
        if (prayer.State == api::DecisionState::Thinking || prayer.State == api::DecisionState::Ready) {
            prayer.State = api::DecisionState::Interrupted;
            prayer.ResolvedAt = {worldId, Epoch, data.Tick, data.Revision};
            // No worker, timeout, action sequence or pending proposal is resumed.
        }
    }
}
} // namespace pixel
