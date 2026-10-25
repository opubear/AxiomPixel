#include "GameSession.h"
#include <algorithm>
#include <stdexcept>

namespace pixel {
api::WorldWillHostStatus GameSession::WorldWillStatus() const {
    const auto &caller = Callers[3];
    if (caller.HighWatermark == UINT64_MAX) throw std::overflow_error("world-will action sequence exhausted");
    return {CurrentCommit(), caller.Id, caller.HighWatermark + 1, AppliedProgram(), Authority.Resources(),
        Ready, Simulation.HasPendingStep(), Paused};
}

api::ObservationAuthorization GameSession::AuthorizeObservation(uint64_t subject, api::Rect scope) const {
    return {Authority.Version(), Ready && Authority.CanObserve(subject, scope)};
}

api::Reason GameSession::AuthorizeAction(api::CallerSessionId caller, const api::Action &action) const {
    const bool intervention = std::holds_alternative<api::InstallIntervention>(action)
        || std::holds_alternative<api::InterveneAtDevice>(action);
    if (caller != WorldWillSession())
        return intervention ? api::Reason::PermissionDenied : api::Reason::None;
    return intervention ? api::Reason::None : api::Reason::PermissionDenied;
}

api::ControlReceipt GameSession::SetWorldWillCapabilities(NativeCaller caller, authority::Capabilities capabilities) {
    if (caller != NativeCaller::Debug && caller != NativeCaller::Driver)
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "PermissionDenied"};
    const auto reason = WriteRejection();
    if (reason != api::Reason::None) return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
    if (!Authority.SetCapabilities(capabilities)) return {api::ControlStatus::Applied};
    const auto changes = ReconcileAuthority();
    Simulation.CommitChange();
    RecordFact(CurrentCommit(), api::AuthorityCause{Authority.Version()},
        api::AuthorityChanged{Authority.Version(), 0, {}, false, capabilities.Observe, capabilities.Intervene});
    RecordAuthorityFacts(changes);
    PublishCommit();
    return {api::ControlStatus::Applied};
}

api::ActionReceipt GameSession::SetDeviceProtection(const api::SetDeviceProtection &action) {
    api::ActionReceipt result;
    const auto *device = Machines.Find(action.Device.Id);
    if (!device || device->Value.Ref != action.Device) {
        result.Rejection = api::Reason::PreconditionFailed;
    } else if (!GetPlayer() || device->Value.Owner != Characters.PlayerSnapshot()->Id.Value) {
        result.Rejection = api::Reason::PermissionDenied;
    } else if (!authority::Control::ValidRegion({{device->Value.X - 16, device->Value.Y - 16}, DeviceWidth + 32, DeviceHeight + 32})) {
        result.Rejection = api::Reason::InvalidArgument;
    } else {
        result.Result = Authority.SetProtection(device->Value, action.Enabled);
        if (action.Enabled) WillMemory.AutonomyChosen = true;
    }
    return result;
}

api::ActionReceipt GameSession::InstallIntervention(const api::InstallIntervention &action, api::RequestKey request) {
    api::ActionReceipt result;
    if (!Authority.CanIntervene(request.Caller, action.Scope)) {
        result.Rejection = api::Reason::PermissionDenied;
        return result;
    }
    if (action.ExpectedResourceVersion != Authority.Resources().Version) {
        result.Rejection = api::Reason::PreconditionFailed;
        return result;
    }
    if (action.Program != AppliedProgram()) {
        result.Rejection = api::Reason::StaleProgram;
        return result;
    }
    if (!Authority.Resources().Available) {
        result.Rejection = api::Reason::LimitExceeded;
        return result;
    }
    if (NextIntervention == UINT64_MAX || Simulation.Tick() > UINT64_MAX - 600)
        throw std::overflow_error("intervention identity or lease exhausted");
    const auto chunk = ChunkOf({action.Origin.X, action.Origin.Y});
    ProcessCarrier carrier;
    carrier.Kind = ProcessCarrierKind::Intervention;
    carrier.Subject = NextIntervention++;
    carrier.Origin = {chunk, {double(action.Origin.X - chunk.X * storage::ChunkEdge),
                             double(action.Origin.Y - chunk.Y * storage::ChunkEdge)}};
    carrier.Scope = {action.Scope.Origin.X, action.Scope.Origin.Y, action.Scope.Width, action.Scope.Height};
    // All descendants share this host-owned carrier, credits, scope and lease.
    if (!GameWorld.GetSimulationBounds().Contains(carrier.Origin.Cell()) || !GameWorld.TryGetCell(action.Origin.X, action.Origin.Y)) {
        result.Rejection = api::Reason::NotReady;
        return result;
    }
    if (!Formulas.CastCarrier(GameWorld, carrier, request)) {
        result.Rejection = api::Reason::LimitExceeded;
        result.Message = Formulas.LastError();
        return result;
    }
    const auto id = Formulas.LastActivationId();
    Interventions.push_back({carrier.Subject, Simulation.Tick() + 600, action.Scope});
    Authority.ReserveIntervention();
    const auto stats = Formulas.Stats();
    result.Result = api::ActivationResult{Epoch, id, AppliedProgram(), stats.Roots, stats.Projectiles, stats.Pending};
    return result;
}

api::ActionReceipt GameSession::InterveneAtDevice(const api::InterveneAtDevice &action, api::RequestKey request) {
    const auto *device = Machines.Find(action.Device.Id);
    if (!device || device->Value.Ref != action.Device) {
        api::ActionReceipt result;
        result.Rejection = api::Reason::PreconditionFailed;
        return result;
    }
    const api::Rect scope{{device->Value.X - 16, device->Value.Y - 16}, 34, 34};
    if (!authority::Control::ValidRegion(scope)) {
        api::ActionReceipt result;
        result.Rejection = api::Reason::InvalidArgument;
        return result;
    }
    return InstallIntervention({action.Program, {device->Value.X + 1, device->Value.Y + 1}, scope,
        action.ExpectedResourceVersion}, request);
}

std::vector<api::FactPayload> GameSession::ReconcileAuthority() {
    if (Simulation.HasPendingStep()) throw std::logic_error("authority changed during prepared step");
    std::vector<api::FactPayload> changes;
    if (!Authority.HasProtection() && Interventions.empty()) return changes;
    for (const auto &change : Authority.Synchronize(Machines.Snapshot())) changes.emplace_back(change);
    if (Interventions.empty()) return changes;
    const auto processes = Formulas.InspectProcesses();
    for (auto it = Interventions.begin(); it != Interventions.end();) {
        const auto process = std::find_if(processes.begin(), processes.end(), [&](const auto &value) {
            return value.Carrier == ProcessCarrierKind::Intervention && value.Subject == it->Id;
        });
        if (process == processes.end()) {
            Authority.ReleaseIntervention();
            it = Interventions.erase(it);
            continue;
        }
        const bool permitted = Authority.CanIntervene(WorldWillSession(), it->Scope);
        const bool expired = Simulation.Tick() >= it->ExpiresAtTick;
        if (!permitted || expired) {
            Formulas.CancelCarrier(GameWorld, ProcessCarrierKind::Intervention, it->Id);
            changes.emplace_back(api::InterventionStopped{{Epoch, process->State.Id}, WorldWillSession(),
                permitted ? api::InterventionStopReason::LeaseExpired : api::InterventionStopReason::ControlRevoked});
            Authority.ReleaseIntervention();
            it = Interventions.erase(it);
        } else ++it;
    }
    return changes;
}

void GameSession::RecordAuthorityFacts(std::span<const api::FactPayload> changes) {
    for (const auto &change : changes)
        RecordFact(CurrentCommit(), api::AuthorityCause{Authority.Version()}, change);
}

} // namespace pixel
