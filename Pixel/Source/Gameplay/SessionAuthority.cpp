#include "GameSession.h"
#include "FormulaBridge/ProcessHost.h"
#include <algorithm>

namespace pixel {
api::ObservationAuthorization GameSession::AuthorizeObservation(uint64_t subject, api::Rect scope) const {
    return {Authority.Version(), Ready && Authority.CanObserve(subject, scope)};
}

api::Reason GameSession::AuthorizeAction(api::CallerSessionId caller, const api::Action &action) const {
    const auto *intervention = std::get_if<api::InstallIntervention>(&action);
    if (caller != WorldWillSession())
        return intervention ? api::Reason::PermissionDenied : api::Reason::None;
    if (!intervention || !Authority.CanIntervene(caller, intervention->Scope)) return api::Reason::PermissionDenied;
    return api::Reason::None;
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
    }
    return result;
}

api::ActionReceipt GameSession::InstallIntervention(const api::InstallIntervention &action, api::RequestKey request) {
    api::ActionReceipt result;
    if (action.Program != AppliedProgram()) {
        result.Rejection = api::Reason::StaleProgram;
        return result;
    }
    if (!Formulas.ActiveProcess()) {
        result.Rejection = api::Reason::Unsupported;
        return result;
    }
    if (Interventions.size() == ProcessHost::MaximumInstances) {
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
    Interventions.push_back({carrier.Subject, request.Caller, Simulation.Tick() + 600, action.Scope});
    const auto stats = Formulas.Stats();
    result.Result = api::ActivationResult{Epoch, id, AppliedProgram(), stats.Roots, stats.Projectiles, stats.Pending};
    return result;
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
            it = Interventions.erase(it);
            continue;
        }
        const bool permitted = Authority.CanIntervene(it->Subject, it->Scope);
        const bool expired = Simulation.Tick() >= it->ExpiresAtTick;
        if (!permitted || expired) {
            Formulas.CancelCarrier(GameWorld, ProcessCarrierKind::Intervention, it->Id);
            changes.emplace_back(api::InterventionStopped{{Epoch, process->State.Id}, it->Subject,
                permitted ? api::InterventionStopReason::LeaseExpired : api::InterventionStopReason::ControlRevoked});
            it = Interventions.erase(it);
        } else ++it;
    }
    return changes;
}

void GameSession::RecordAuthorityFacts(std::span<const api::FactPayload> changes) {
    for (const auto &change : changes)
        RecordFact(CurrentCommit(), api::AuthorityCause{Authority.Version()}, change);
}

bool GameSession::HasTransientAuthority() const {
    return Authority.HasProtection() || !Interventions.empty() || !Authority.DefaultCapabilities();
}
} // namespace pixel
