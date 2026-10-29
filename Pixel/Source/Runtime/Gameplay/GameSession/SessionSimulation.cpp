#include "GameSession.h"

namespace pixel {
GameSession::~GameSession() { Worker.reset(); }

void GameSession::EnableSimulationThread() {
    if (!Worker) Worker = std::make_unique<SimulationWorker>();
}

void GameSession::BeginFrame() {
    if (Worker) Worker->Invoke([this] { Formulas.BeginFrame(); });
    else Formulas.BeginFrame();
}

void GameSession::ClearPlayerInput() {
    if (Worker) Worker->Invoke([this] { Simulation.ClearInput(); });
    else Simulation.ClearInput();
}

void GameSession::Tick(float duration, PlayerInput input) {
    RunFrame(duration, input, [] {});
}

void GameSession::RunFrame(float duration, PlayerInput input, const std::function<void()> &game, bool profile) {
    if (Worker) Worker->Run([=, this] { TickSimulation(duration, input); }, game, profile);
    else { TickSimulation(duration, input); game(); }
}

PlayerPresentation GameSession::CapturePlayerPresentation() const {
    PlayerPresentation result;
    result.Base = CurrentCommit();
    result.Player = Characters.PlayerSnapshot();
    if (const auto *player = GetPlayer()) result.Motion = *player;
    result.Terrain = GameWorld.ReadBuffers();
    result.Clock = Simulation;
    result.Time = Simulation.Time();
    result.Advancing = Ready && !Simulation.HasPendingStep() && (!Paused || Simulation.HasStepRequest());
    return result;
}

std::optional<api::ActionReceipt> GameSession::FindAction(api::RequestKey key) const {
    if (key.WorldEpoch != Epoch) return {};
    for (const auto &pending : PendingActions)
        if (pending.Receipt.Key == key) return pending.Receipt;
    for (const auto &caller : Callers)
        if (caller.Id == key.Caller)
            for (const auto &receipt : caller.Receipts)
                if (receipt.Key == key) return receipt;
    return {};
}

bool GameSession::CancelQueuedAction(api::RequestKey key) {
    // Applied commands belong to an in-progress step and cannot be withdrawn.
    if (ActionsPrepared) return false;
    const auto it = std::find_if(PendingActions.begin(), PendingActions.end(),
        [&](const auto &pending) { return pending.Receipt.Key == key; });
    if (it == PendingActions.end()) return false;
    auto receipt = it->Receipt;
    receipt.Status = api::ActionStatus::Rejected;
    receipt.Rejection = api::Reason::PreconditionFailed;
    receipt.Message = "Cancelled before simulation";
    receipt.ResolvedAt = CurrentCommit();
    RememberReceipt(std::move(receipt));
    PendingActions.erase(it);
    return true;
}

void GameSession::RememberReceipt(api::ActionReceipt receipt) {
    auto *caller = FindCaller(receipt.Key.Caller);
    if (!caller) throw std::logic_error("action completion belongs to an expired caller");
    const auto position = std::upper_bound(caller->Receipts.begin(), caller->Receipts.end(), receipt.Key.Sequence,
        [](uint64_t sequence, const auto &previous) { return sequence < previous.Key.Sequence; });
    caller->Receipts.insert(position, std::move(receipt));
    if (caller->Receipts.size() > 64) caller->Receipts.pop_front();
}

void GameSession::ApplyPendingActions() {
    if (ActionsPrepared) return;
    ActionsPrepared = true;
    DeferredPublication = Worker != nullptr;
    ProcessingActions = true;
    try {
        for (auto &pending : PendingActions)
            pending.Result = Submit(pending.Caller, pending.Request);
    } catch (...) {
        ProcessingActions = false;
        throw;
    }
    ProcessingActions = false;
}

void GameSession::CompletePendingActions() {
    for (auto &pending : PendingActions) {
        if (!pending.Result) throw std::logic_error("simulation committed an unprocessed action");
        pending.Result->ResolvedAt = CurrentCommit();
        RememberReceipt(std::move(*pending.Result));
    }
    PendingActions.clear();
    ActionsPrepared = false;
    DeferredPublication = false;
}
} // namespace pixel
