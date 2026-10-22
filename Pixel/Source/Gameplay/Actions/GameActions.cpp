#include "Gameplay/GameSession.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <type_traits>

namespace pixel::api {
const char *ReasonName(Reason reason) {
    switch (reason) {
#define PIXEL_REASON(name) case Reason::name: return #name
    PIXEL_REASON(None); PIXEL_REASON(InvalidArgument); PIXEL_REASON(Unsupported);
    PIXEL_REASON(StaleEpoch); PIXEL_REASON(CallerExpired); PIXEL_REASON(DuplicateExpired);
    PIXEL_REASON(SessionUnavailable); PIXEL_REASON(WaitingForBudget); PIXEL_REASON(PermissionDenied);
    PIXEL_REASON(NotReady); PIXEL_REASON(Blocked); PIXEL_REASON(PreconditionFailed);
    PIXEL_REASON(StaleProgram); PIXEL_REASON(LaunchBlocked); PIXEL_REASON(LimitExceeded);
    PIXEL_REASON(StorageFailure); PIXEL_REASON(InvalidProgram);
#undef PIXEL_REASON
    }
    throw std::logic_error("unknown action rejection reason");
}
} // namespace pixel::api

namespace pixel {
namespace {
// Host identities are never reused across Session objects or world replacements.
uint64_t NewIdentity() {
    static std::atomic<uint64_t> next{1};
    const auto id = next.fetch_add(1);
    if (id == UINT64_MAX) std::terminate();
    return id;
}
bool ValidCell(api::Cell cell) {
    // Leave the extreme chunk to internal muzzle offsets and neighbourhood reads.
    constexpr int64_t limit = (int64_t{1} << 49) - 1;
    const auto chunk = ChunkOf({cell.X, cell.Y});
    return chunk.X > -limit && chunk.X < limit && chunk.Y > -limit && chunk.Y < limit;
}
bool ValidPosition(const api::Position &position) {
    return ValidCell(position.AnchorCell) && std::all_of(position.Offset.begin(), position.Offset.end(),
        [](double value) { return std::isfinite(value) && value >= 0 && value < 1; });
}
WorldPosition WorldPoint(const api::Position &position) {
    const CellCoord cell{position.AnchorCell.X, position.AnchorCell.Y};
    const auto chunk = ChunkOf(cell);
    return {chunk, {double(cell.X - chunk.X * storage::ChunkEdge) + position.Offset[0],
                    double(cell.Y - chunk.Y * storage::ChunkEdge) + position.Offset[1]}};
}
api::Reason Validate(const api::Action &action) {
    return std::visit([](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, api::MineRegion>) {
            if (!ValidCell(value.Target) || (value.ExpectedMaterial && !IsKnownCellType(CellTypeId(*value.ExpectedMaterial))))
                return api::Reason::InvalidArgument;
        } else if constexpr (std::is_same_v<T, api::PlaceMaterial>) {
            if (!ValidCell(value.Target)) return api::Reason::InvalidArgument;
            if (value.MaterialId != uint32_t(CellTypeId::Dirt)) return api::Reason::Unsupported;
        } else if constexpr (std::is_same_v<T, api::ActivateProgram>) {
            if (!ValidPosition(value.Target)) return api::Reason::InvalidArgument;
        } else if constexpr (std::is_same_v<T, api::SelectWindStrategy>) {
            if (value.Kind < api::WindKind::Coarse || value.Kind > api::WindKind::Empty)
                return api::Reason::Unsupported;
        } else if constexpr (std::is_same_v<T, api::SetWindBoundary>) {
            if (!ValidCell({0, value.Y})) return api::Reason::InvalidArgument;
        }
        return api::Reason::None;
    }, action);
}
WindStrategyKind WorldWind(api::WindKind kind) {
    switch (kind) {
    case api::WindKind::Coarse: return WindStrategyKind::Coarse;
    case api::WindKind::Reference: return WindStrategyKind::Reference;
    case api::WindKind::Procedural: return WindStrategyKind::Procedural;
    case api::WindKind::Hybrid: return WindStrategyKind::Hybrid;
    case api::WindKind::Adaptive: return WindStrategyKind::Adaptive;
    case api::WindKind::Lbm: return WindStrategyKind::Lbm;
    case api::WindKind::Empty: return WindStrategyKind::Empty;
    }
    throw std::logic_error("unvalidated wind kind");
}
api::WindKind PublicWind(WindStrategyKind kind) {
    switch (kind) {
    case WindStrategyKind::Coarse: return api::WindKind::Coarse;
    case WindStrategyKind::Reference: return api::WindKind::Reference;
    case WindStrategyKind::Procedural: return api::WindKind::Procedural;
    case WindStrategyKind::Hybrid: return api::WindKind::Hybrid;
    case WindStrategyKind::Adaptive: return api::WindKind::Adaptive;
    case WindStrategyKind::Lbm: return api::WindKind::Lbm;
    case WindStrategyKind::Empty: return api::WindKind::Empty;
    }
    throw std::logic_error("unknown internal wind kind");
}
} // namespace

void GameSession::RenewActionSessions() {
    Epoch = NewIdentity();
    for (size_t i = 0; i < Callers.size(); ++i) {
        Callers[i] = {};
        Callers[i].Id = NewIdentity();
        Callers[i].Debug = i != size_t(NativeCaller::Input);
    }
}
api::CallerSessionId GameSession::CallerSession(NativeCaller caller) const {
    return Callers.at(size_t(caller)).Id;
}
const api::ActionReceipt *GameSession::LastAction(NativeCaller caller) const {
    const auto &receipts = Callers.at(size_t(caller)).Receipts;
    return receipts.empty() ? nullptr : &receipts.back();
}
GameSession::CallerState *GameSession::FindCaller(api::CallerSessionId caller) {
    for (auto &state : Callers)
        if (state.Id == caller && caller != 0) return &state;
    return nullptr;
}
api::Reason GameSession::WriteRejection() const {
    if (!Ready) return api::Reason::SessionUnavailable;
    if (GameWorld.GetEnvironment().IsWaitingForBudget()) return api::Reason::WaitingForBudget;
    return api::Reason::None;
}
api::ActionReceipt GameSession::Execute(NativeCaller caller, api::Action action) {
    const auto id = CallerSession(caller);
    const auto high = FindCaller(id)->HighWatermark;
    if (high == UINT64_MAX) throw std::overflow_error("action sequence exhausted");
    return Submit(id, {Epoch, high + 1, std::move(action)});
}
api::ActionReceipt GameSession::Submit(api::CallerSessionId caller, const api::ActionRequest &request) {
    api::ActionReceipt receipt;
    receipt.Key = {request.WorldEpoch, caller, request.Sequence};
    const auto reject = [&](api::Reason reason) {
        receipt.Rejection = reason;
        receipt.Message = api::ReasonName(reason);
        return receipt;
    };
    auto *state = FindCaller(caller);
    if (!state) return reject(api::Reason::CallerExpired);
    if (request.WorldEpoch != Epoch) return reject(api::Reason::StaleEpoch);
    if (request.Sequence == 0) return reject(api::Reason::InvalidArgument);
    if (request.Sequence <= state->HighWatermark) {
        for (const auto &previous : state->Receipts)
            if (previous.Key.Sequence == request.Sequence) return previous;
        return reject(api::Reason::DuplicateExpired);
    }

    api::Reason reason = request.SchemaVersion != 1
        ? api::Reason::InvalidArgument : Validate(request.Payload);
    if (reason == api::Reason::None) reason = WriteRejection();
    const bool wind = std::holds_alternative<api::SelectWindStrategy>(request.Payload) ||
                      std::holds_alternative<api::SetWindBoundary>(request.Payload);
    if (reason == api::Reason::None && wind && !state->Debug) reason = api::Reason::PermissionDenied;
    if (reason != api::Reason::None) {
        receipt = reject(reason);
    } else {
        receipt = Dispatch(request.Payload);
        receipt.Key = {request.WorldEpoch, caller, request.Sequence};
    }
    // Terminal refusals also consume sequence numbers. Storage stays bounded.
    state->HighWatermark = std::max(state->HighWatermark, request.Sequence);
    if (state->Receipts.size() == 64) state->Receipts.pop_front();
    state->Receipts.push_back(receipt);
    return receipt;
}

api::ActionReceipt GameSession::Dispatch(const api::Action &action) {
    api::ActionReceipt receipt;
    std::visit([&](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, api::MineRegion> || std::is_same_v<T, api::PlaceMaterial>) {
            api::MaterialResult result{value.Target};
            const auto before = GameWorld.TryGetCell(value.Target.X, value.Target.Y);
            if (!before) {
                receipt.Rejection = api::Reason::NotReady;
                result.NotReady = 1;
            } else {
                result.Before = result.After = uint32_t(before->Type);
                if constexpr (std::is_same_v<T, api::MineRegion>) {
                    if (value.ExpectedMaterial && *value.ExpectedMaterial != *result.Before)
                        receipt.Rejection = api::Reason::PreconditionFailed;
                }
                if (receipt.Rejection == api::Reason::None) {
                    const auto edited = [&] {
                        if constexpr (std::is_same_v<T, api::MineRegion>)
                            return GameWorld.TryRemoveMaterial(value.Target.X, value.Target.Y);
                        else return GameWorld.TryPlaceDirt(value.Target.X, value.Target.Y);
                    }();
                    if (edited == CellEditResult::Applied) {
                        result.Changed = 1;
                        result.After = uint32_t(GameWorld.GetCell(value.Target.X, value.Target.Y).Type);
                    } else if (edited == CellEditResult::Blocked) {
                        receipt.Rejection = api::Reason::Blocked;
                        result.Blocked = 1;
                    } else {
                        throw std::logic_error("resident material disappeared during synchronous action");
                    }
                }
            }
            receipt.Result = result;
        } else if constexpr (std::is_same_v<T, api::ActivateProgram>) {
            if (value.Program != AppliedProgram()) {
                receipt.Rejection = api::Reason::StaleProgram;
            } else {
                receipt.Rejection = ActivateAt(WorldPoint(value.Target));
                receipt.Message = FormulaError;
                if (receipt.Rejection == api::Reason::None) {
                    const auto stats = Formulas.Stats();
                    receipt.Result = api::ActivationResult{Epoch, Formulas.LastActivationId(), AppliedProgram(),
                        stats.Roots, stats.Projectiles, stats.Pending};
                }
            }
        } else {
            if constexpr (std::is_same_v<T, api::SelectWindStrategy>) SelectWindStrategy(WorldWind(value.Kind));
            else SetWindBoundary(value.Y);
            receipt.Result = api::WindResult{PublicWind(WindKind), GameWorld.GetWindBoundary()};
        }
    }, action);
    receipt.Status = receipt.Rejection == api::Reason::None ? api::ActionStatus::Applied : api::ActionStatus::Rejected;
    if (receipt.Message.empty() && receipt.Rejection != api::Reason::None)
        receipt.Message = api::ReasonName(receipt.Rejection);
    return receipt;
}

api::ControlReceipt GameSession::Control(api::CallerSessionId caller, const api::SessionControl &control) {
    const auto *state = FindCaller(caller);
    if (!state) return {api::ControlStatus::Rejected, api::Reason::CallerExpired, "CallerExpired"};
    if (!state->Debug && !std::holds_alternative<api::ResetWorld>(control))
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "PermissionDenied"};
    return std::visit([&](const auto &value) -> api::ControlReceipt {
        using T = std::decay_t<decltype(value)>;
        // Save/reset preserve v4 deferred behavior until M2. Clear/apply are world writes.
        if constexpr (std::is_same_v<T, api::ClearRunning>) {
            const auto reason = WriteRejection();
            if (reason != api::Reason::None) return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
            Formulas.Clear();
        } else if constexpr (std::is_same_v<T, api::SaveWorld> || std::is_same_v<T, api::ResetWorld> || std::is_same_v<T, api::RecoverWorld>) {
            if constexpr (!std::is_same_v<T, api::RecoverWorld>)
                if (!Ready) return {api::ControlStatus::Rejected, api::Reason::SessionUnavailable, "SessionUnavailable"};
            const bool success = [&] {
                if constexpr (std::is_same_v<T, api::SaveWorld>) return TrySave();
                else if constexpr (std::is_same_v<T, api::ResetWorld>) return TryReset();
                else return TryRecover();
            }();
            if (!success) return {api::ControlStatus::Failed, api::Reason::StorageFailure, SaveError};
            if constexpr (std::is_same_v<T, api::SaveWorld>) return {api::ControlStatus::Saved};
            if constexpr (std::is_same_v<T, api::ResetWorld>) return {api::ControlStatus::Replaced};
        } else if constexpr (std::is_same_v<T, api::SetNextGenerator>) {
            if (value.Kind != api::GeneratorKind::Flat && value.Kind != api::GeneratorKind::Natural)
                return {api::ControlStatus::Rejected, api::Reason::Unsupported, "Unsupported"};
            NextGeneration.Kind = value.Kind == api::GeneratorKind::Flat ? WorldGeneratorKind::Flat : WorldGeneratorKind::Natural;
        } else if constexpr (std::is_same_v<T, api::SetNextSeed>) {
            NextGeneration.Seed = value.Seed;
        } else {
            if (!Ready) return {api::ControlStatus::Rejected, api::Reason::SessionUnavailable, "SessionUnavailable"};
            if constexpr (std::is_same_v<T, api::SetPaused>) Paused = value.Paused;
            else if constexpr (std::is_same_v<T, api::StepSimulation>) StepRequested = true;
            else if constexpr (std::is_same_v<T, api::RetryStreaming>) GameWorld.RetryStreaming();
        }
        return {api::ControlStatus::Applied};
    }, control);
}

ApplyProgramResult GameSession::ApplyProgram(api::CallerSessionId caller, const formula::Formula &description) {
    const auto *state = FindCaller(caller);
    api::Reason reason = !state ? api::Reason::CallerExpired :
        !state->Debug ? api::Reason::PermissionDenied : WriteRejection();
    if (reason != api::Reason::None) return {reason, {}, api::ReasonName(reason)};
    if (!Formulas.Apply(description)) return {api::Reason::InvalidProgram, {}, Formulas.LastError()};
    return {api::Reason::None, AppliedProgram(), {}};
}
} // namespace pixel
