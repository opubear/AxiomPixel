#include "GameSession.h"
#include <algorithm>

namespace pixel {
uint64_t GameSession::BeginPrayer(const api::CommitStamp &observed, api::Rect scope) {
    if (WriteRejection() != api::Reason::None || observed.WorldEpoch != Epoch
        || observed.WorldId != CurrentCommit().WorldId || !authority::Control::ValidRegion(scope))
        throw std::logic_error("prayer admitted outside its complete game session");
    if (WillMemory.NextPrayer == UINT64_MAX) throw std::overflow_error("prayer identity exhausted");
    if (WillMemory.Records.size() == api::WorldWillMemory::Capacity) {
        const auto retired = std::find_if(WillMemory.Records.begin(), WillMemory.Records.end(),
            [](const auto &record) { return record.State != api::DecisionState::Thinking; });
        if (retired == WillMemory.Records.end()) throw std::logic_error("prayer concurrency exceeded journal capacity");
        WillMemory.Records.erase(retired);
    }
    api::PrayerRecord record;
    record.Id = WillMemory.NextPrayer++;
    record.ObservedAt = observed;
    record.Scope = scope;
    WillMemory.Records.push_back(record);
    CommitChange();
    return record.Id;
}

void GameSession::ResolvePrayer(uint64_t id, api::DecisionState state, std::string explanation,
                               const std::optional<api::ActionReceipt> &receipt) {
    if (WriteRejection() != api::Reason::None) throw std::logic_error("prayer resolution during prepared step");
    auto found = std::find_if(WillMemory.Records.begin(), WillMemory.Records.end(),
        [&](const auto &record) { return record.Id == id; });
    if (found == WillMemory.Records.end() || found->ObservedAt.WorldEpoch != Epoch
        || found->State != api::DecisionState::Thinking || state == api::DecisionState::Thinking
        || state == api::DecisionState::Ready || explanation.size() > api::WorldWillMemory::ExplanationBytes)
        throw std::logic_error("invalid prayer lifecycle transition");
    if ((state == api::DecisionState::Applied) != (receipt && receipt->Applied()))
        throw std::logic_error("prayer result disagrees with committed action");
    found->State = state;
    found->Explanation = std::move(explanation);
    if (receipt) {
        if (receipt->Key.WorldEpoch != Epoch || receipt->Key.Caller != WorldWillSession())
            throw std::logic_error("prayer action belongs to another subject");
        found->Action = receipt->Key;
        found->Facts = receipt->Facts;
        found->Rejection = receipt->Rejection;
        if (const auto *activation = std::get_if<api::ActivationResult>(&receipt->Result))
            found->InstanceId = activation->ActivationId;
    }
    if (state == api::DecisionState::Applied) {
        const auto history = Knowledge.Export();
        const auto evidence = std::find_if(history.History.rbegin(), history.History.rend(), [&](const auto &record) {
            const auto *activation = std::get_if<api::ProgramActivated>(&record.Source.Payload);
            const auto *request = std::get_if<api::RequestKey>(&record.Source.Cause);
            return activation && request && *request == *found->Action && activation->ActivationId == found->InstanceId;
        });
        if (evidence == history.History.rend()) throw std::logic_error("applied prayer has no activation evidence");
        found->EvidenceSequence = evidence->Sequence;
        if (WillMemory.AidAccepted == UINT64_MAX) throw std::overflow_error("aid history exhausted");
        ++WillMemory.AidAccepted;
    }
    Simulation.CommitChange();
    found->ResolvedAt = CurrentCommit();
    PublishCommit();
}
} // namespace pixel
