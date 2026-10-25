#include "GameSession.h"

namespace pixel {
api::ObservationResult GameSession::Observe(NativeCaller observer, api::Rect scope) {
    auto result = Observations.Capture(size_t(observer), scope, GameWorld, Characters, Formulas, GameplayValues, CurrentCommit(),
        Paused, Ready, Simulation.HasPendingStep(), Facts.HighWatermark());
    if (result.Snapshot) result.Snapshot->Knowledge = Knowledge.Capture(result.Snapshot->Stamp);
    return result;
}

api::ObservationResult GameSession::CaptureObservation(api::Rect scope) {
    auto result = Observations.Capture(3, scope, GameWorld, Characters, Formulas, GameplayValues, CurrentCommit(),
        Paused, Ready, Simulation.HasPendingStep(), Facts.HighWatermark());
    // LLMView owns its frozen copy. No abandoned external cache keeps sampling
    // every commit after its view is closed or its lease expires.
    Observations.Close(3);
    if (result.Snapshot) result.Snapshot->Knowledge = Knowledge.Capture(result.Snapshot->Stamp);
    return result;
}

api::ObservationMetrics GameSession::ObservationStats() const {
    auto result = Observations.Metrics();
    const auto effects = Formulas.Stats().EffectRecordBytes;
    result.RetainedBytes += Facts.RetainedBytes() + effects + GameplayValues.RetainedBytes() + Knowledge.RetainedBytes();
    result.MaximumRetainedBytes += DomainFactBuffer::MaximumRetainedBytes + effects + api::GameplayObservation::MaximumRetainedBytes + 256 * 1024;
    return result;
}

api::FactId GameSession::RecordFact(const api::CommitStamp &stamp, api::FactCause cause, api::FactPayload payload) {
    const auto id = Facts.Append(stamp, cause, payload);
    // Feed important history directly: a large committed batch may already
    // have evicted early entries from the transient domain-fact ring.
    Knowledge.Record({id, stamp, std::move(cause), std::move(payload)});
    return id;
}

void GameSession::RecordFormulaFacts() { RecordFormulaFacts(Formulas.LastEffects()); }

void GameSession::RecordFormulaFacts(std::span<const FormulaEffectRecord> effects) {
    const auto stamp = CurrentCommit();
    for (const auto &effect : effects) {
        api::ProgramEffectCause cause{{Epoch, effect.CastId}, {effect.ResourceId, effect.Version},
            effect.EmissionSequence, effect.ActivationRequest, {}};
        auto payload = effect.Payload;
        if (auto *wind = std::get_if<api::WindPulseSubmitted>(&payload)) wind->Instance.WorldEpoch = Epoch;
        RecordFact(stamp, cause, std::move(payload));
    }
}

void GameSession::RecordActionFacts(api::ActionReceipt &receipt, api::WindConfiguration beforeWind) {
    const auto stamp = CurrentCommit();
    const auto first = Facts.HighWatermark() + 1;
    if (const auto *material = std::get_if<api::MaterialResult>(&receipt.Result)) {
        if (!material->Before || !material->After || material->Changed != 1)
            throw std::logic_error("applied material action has no actual result");
        if (*material->After == uint32_t(CellTypeId::Air))
            RecordFact(stamp, receipt.Key, api::MaterialRemoved{material->Target, *material->Before, *material->After});
        else
            RecordFact(stamp, receipt.Key, api::MaterialPlaced{material->Target, *material->Before, *material->After});
    } else if (const auto *activation = std::get_if<api::ActivationResult>(&receipt.Result)) {
        RecordFact(stamp, receipt.Key, api::ProgramActivated{activation->Program, activation->ActivationId});
        RecordFormulaFacts();
    } else if (const auto *wind = std::get_if<api::WindResult>(&receipt.Result)) {
        RecordFact(stamp, receipt.Key, api::WindConfigured{beforeWind, {wind->Kind, wind->BoundaryY}});
    } else if (const auto *device = std::get_if<api::DeviceChanged>(&receipt.Result)) {
        RecordFact(stamp, receipt.Key, *device);
    } else if (const auto *authority = std::get_if<api::AuthorityChanged>(&receipt.Result)) {
        RecordFact(stamp, receipt.Key, *authority);
    } else if (const auto *observed = std::get_if<api::ForceObserved>(&receipt.Result)) {
        RecordFact(stamp, receipt.Key, *observed);
    } else {
        throw std::logic_error("applied action has no fact payload");
    }
    receipt.Facts = {Epoch, first, uint32_t(Facts.HighWatermark() - first + 1)};
}
} // namespace pixel
