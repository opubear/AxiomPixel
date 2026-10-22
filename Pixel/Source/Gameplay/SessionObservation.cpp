#include "GameSession.h"

namespace pixel {
api::ObservationResult GameSession::Observe(NativeCaller observer, api::Rect scope) {
    return Observations.Capture(size_t(observer), scope, GameWorld, Formulas, CurrentCommit(),
        Simulation.IsPaused(), Ready, Simulation.HasPendingStep(), Facts.HighWatermark());
}

api::ObservationMetrics GameSession::ObservationStats() const {
    auto result = Observations.Metrics();
    const auto effects = Formulas.Stats().EffectRecordBytes;
    result.RetainedBytes += Facts.RetainedBytes() + effects;
    result.MaximumRetainedBytes += DomainFactBuffer::MaximumRetainedBytes + effects;
    return result;
}

void GameSession::RecordFormulaFacts() {
    const auto stamp = CurrentCommit();
    for (const auto &effect : Formulas.LastEffects()) {
        api::ProgramEffectCause cause{{Epoch, effect.CastId}, {1, effect.Version},
            effect.EmissionSequence, effect.ActivationRequest, {}};
        auto payload = effect.Payload;
        if (auto *wind = std::get_if<api::WindPulseSubmitted>(&payload)) wind->Instance.WorldEpoch = Epoch;
        Facts.Append(stamp, cause, std::move(payload));
    }
}

void GameSession::RecordActionFacts(api::ActionReceipt &receipt, api::WindConfiguration beforeWind) {
    const auto stamp = CurrentCommit();
    const auto first = Facts.HighWatermark() + 1;
    if (const auto *material = std::get_if<api::MaterialResult>(&receipt.Result)) {
        if (!material->Before || !material->After || material->Changed != 1)
            throw std::logic_error("applied material action has no actual result");
        if (*material->After == uint32_t(CellTypeId::Air))
            Facts.Append(stamp, receipt.Key, api::MaterialRemoved{material->Target, *material->Before, *material->After});
        else
            Facts.Append(stamp, receipt.Key, api::MaterialPlaced{material->Target, *material->Before, *material->After});
    } else if (const auto *activation = std::get_if<api::ActivationResult>(&receipt.Result)) {
        Facts.Append(stamp, receipt.Key, api::ProgramActivated{activation->Program, activation->ActivationId});
        RecordFormulaFacts();
    } else if (const auto *wind = std::get_if<api::WindResult>(&receipt.Result)) {
        Facts.Append(stamp, receipt.Key, api::WindConfigured{beforeWind, {wind->Kind, wind->BoundaryY}});
    } else {
        throw std::logic_error("applied action has no fact payload");
    }
    receipt.Facts = {Epoch, first, uint32_t(Facts.HighWatermark() - first + 1)};
}
} // namespace pixel
