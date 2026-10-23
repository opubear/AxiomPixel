#include "Gameplay/GameSession.h"
#include <algorithm>
#include <atomic>
#include <limits>
#include <type_traits>

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
        } else if constexpr (std::is_same_v<T, api::PlaceDevice>) {
            if (!ValidCell(value.Target)) return api::Reason::InvalidArgument;
        } else if constexpr (std::is_same_v<T, api::RemoveDevice> || std::is_same_v<T, api::SupplyDevice> ||
                             std::is_same_v<T, api::SetDeviceEnabled>) {
            if (!value.Device.Id || !value.Device.Version) return api::Reason::InvalidArgument;
            if constexpr (std::is_same_v<T, api::SupplyDevice>)
                if (!value.Quantity || value.Quantity > 64) return api::Reason::InvalidArgument;
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
    receipt.ResolvedAt = CurrentCommit();
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
        const api::WindConfiguration beforeWind{PublicWind(WindKind), GameWorld.GetWindBoundary()};
        receipt = Dispatch(request.Payload, {request.WorldEpoch, caller, request.Sequence});
        receipt.Key = {request.WorldEpoch, caller, request.Sequence};
        if (receipt.Applied()) {
            Simulation.CommitChange();
            RecordActionFacts(receipt, beforeWind);
            PublishCommit();
        }
        receipt.ResolvedAt = CurrentCommit();
    }
    // Terminal refusals also consume sequence numbers. Storage stays bounded.
    state->HighWatermark = std::max(state->HighWatermark, request.Sequence);
    if (state->Receipts.size() == 64) state->Receipts.pop_front();
    state->Receipts.push_back(receipt);
    return receipt;
}

api::ActionReceipt GameSession::Dispatch(const api::Action &action, api::RequestKey request) {
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
                    if constexpr (std::is_same_v<T, api::MineRegion>) {
                        const auto mined = MineCell(GameWorld, {value.Target.X, value.Target.Y}, MiningCollector{Inventory, PlayerBag});
                        switch (mined.Status) {
                        case MiningStatus::Applied: result.Collected = mined.Yield; break;
                        case MiningStatus::Blocked: receipt.Rejection = api::Reason::Blocked; break;
                        case MiningStatus::InventoryFull: receipt.Rejection = api::Reason::InventoryFull; break;
                        case MiningStatus::NotReady: throw std::logic_error("resident mining became unavailable");
                        }
                    } else {
                        if (before->Type != CellTypeId::Air || Characters.Occupies({value.Target.X, value.Target.Y})
                            || Machines.FindAt(value.Target.X, value.Target.Y)) {
                            receipt.Rejection = api::Reason::Blocked;
                        } else {
                            const std::array cost{items::ItemAmount{items::ItemId::Soil, 1}};
                            const auto reserved = Inventory.Reserve(Inventory.Reference(PlayerBag), cost);
                            if (reserved.Status == items::Result::InsufficientItems)
                                receipt.Rejection = api::Reason::InsufficientItems;
                            else if (reserved.Status == items::Result::ReservationLimit)
                                receipt.Rejection = api::Reason::LimitExceeded;
                            else {
                                if (reserved.Status != items::Result::Applied ||
                                    GameWorld.TryPlaceDirt(value.Target.X, value.Target.Y) != CellEditResult::Applied ||
                                    Inventory.Consume(reserved.Id) != items::Result::Applied)
                                    throw std::logic_error("preflighted material placement failed");
                                result.Consumed = cost.front();
                            }
                        }
                    }
                    if (receipt.Rejection == api::Reason::None) {
                        result.Changed = 1;
                        result.After = uint32_t(GameWorld.GetCell(value.Target.X, value.Target.Y).Type);
                    } else if (receipt.Rejection == api::Reason::Blocked) {
                        result.Blocked = 1;
                    }
                }
            }
            receipt.Result = result;
        } else if constexpr (std::is_same_v<T, api::ActivateProgram>) {
            if (value.Program != AppliedProgram()) {
                receipt.Rejection = api::Reason::StaleProgram;
            } else {
                receipt.Rejection = ActivateAt(WorldPoint(value.Target), request);
                receipt.Message = FormulaError;
                if (receipt.Rejection == api::Reason::None) {
                    const auto stats = Formulas.Stats();
                    receipt.Result = api::ActivationResult{Epoch, Formulas.LastActivationId(), AppliedProgram(),
                        stats.Roots, stats.Projectiles, stats.Pending};
                }
            }
        } else if constexpr (std::is_same_v<T, api::PlaceDevice>) {
            receipt = PlaceDevice(value.Target);
        } else if constexpr (std::is_same_v<T, api::RemoveDevice> || std::is_same_v<T, api::SupplyDevice> ||
                             std::is_same_v<T, api::SetDeviceEnabled>) {
            receipt = ChangeDevice(value.Device, action);
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
    auto result = std::visit([&](const auto &value) -> api::ControlReceipt {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, api::ClearRunning>) {
            const auto reason = WriteRejection();
            if (reason != api::Reason::None) return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
            const bool changed = Formulas.Stats().Roots != 0;
            Formulas.Clear(GameWorld);
            const auto stopped = StopDevices();
            if (changed || !stopped.empty()) {
                Simulation.CommitChange();
                for (const auto &change : stopped)
                    Facts.Append(CurrentCommit(), api::DeviceCause{change.Device.Ref.Id, 0}, change);
                PublishCommit();
            }
        } else if constexpr (std::is_same_v<T, api::SaveWorld> || std::is_same_v<T, api::ResetWorld> || std::is_same_v<T, api::RecoverWorld>) {
            if constexpr (!std::is_same_v<T, api::RecoverWorld>) {
                if (!Ready) return {api::ControlStatus::Rejected, api::Reason::SessionUnavailable, "SessionUnavailable"};
                if (Simulation.HasPendingStep())
                    return {api::ControlStatus::Deferred, api::Reason::WaitingForBudget,
                            "Save/reset deferred: simulation is waiting for storage budget. The session stays open; resolve the budget shortage, finish the step, then retry."};
            }
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
            if constexpr (std::is_same_v<T, api::SetPaused>) {
                if (value.Paused != Simulation.IsPaused()) {
                    const auto reason = WriteRejection();
                    if (reason != api::Reason::None) return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
                    Simulation.SetPaused(value.Paused);
                    CommitChange();
                }
            } else if constexpr (std::is_same_v<T, api::StepSimulation>) Simulation.RequestStep();
            else if constexpr (std::is_same_v<T, api::RetryStreaming>) GameWorld.RetryStreaming();
        }
        return {api::ControlStatus::Applied};
    }, control);
    if (std::holds_alternative<api::SaveWorld>(control) || std::holds_alternative<api::ResetWorld>(control)
        || std::holds_alternative<api::RecoverWorld>(control))
        PersistenceResult = result;
    return result;
}

ApplyProgramResult GameSession::ApplyProgram(api::CallerSessionId caller, const formula::Formula &description) {
    const auto *state = FindCaller(caller);
    api::Reason reason = !state ? api::Reason::CallerExpired :
        !state->Debug ? api::Reason::PermissionDenied : WriteRejection();
    if (reason != api::Reason::None) return {reason, {}, api::ReasonName(reason)};
    if (!Formulas.Apply(description)) return {api::Reason::InvalidProgram, {}, Formulas.LastError()};
    CommitChange();
    return {api::Reason::None, AppliedProgram(), {}};
}

ApplyProgramResult GameSession::ApplyProcess(NativeCaller caller, const formula::process::ProgramArtifact &program) {
    if (caller == NativeCaller::Input) return {api::Reason::PermissionDenied, {}, "PermissionDenied"};
    const auto reason = WriteRejection();
    if (reason != api::Reason::None) return {reason, {}, api::ReasonName(reason)};
    Formulas.ApplyProcess(program);
    CommitChange();
    return {api::Reason::None, AppliedProgram(), {}};
}

api::ControlReceipt GameSession::StartProcessFixture(NativeCaller caller, const ProcessCarrier &carrier) {
    if (caller == NativeCaller::Input || carrier.Kind == ProcessCarrierKind::Player || carrier.Kind == ProcessCarrierKind::Device)
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "Fixture requires native host assembly"};
    const auto reason = WriteRejection();
    if (reason != api::Reason::None) return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
    if (!Formulas.CastCarrier(GameWorld, carrier))
        return {api::ControlStatus::Rejected, api::Reason::InvalidProgram, Formulas.LastError()};
    Simulation.CommitChange();
    const api::ProgramEffectCause cause{{Epoch, Formulas.LastActivationId()}, AppliedProgram(), 0, {}, {}};
    Facts.Append(CurrentCommit(), cause, api::ProgramActivated{AppliedProgram(), Formulas.LastActivationId()});
    RecordFormulaFacts();
    PublishCommit();
    return {api::ControlStatus::Applied};
}

api::ControlReceipt GameSession::CancelProcessCarrier(NativeCaller caller, ProcessCarrierKind kind, uint64_t subject) {
    if (caller == NativeCaller::Input || kind == ProcessCarrierKind::Device)
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "PermissionDenied"};
    const auto reason = WriteRejection();
    if (reason != api::Reason::None) return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
    Formulas.CancelCarrier(GameWorld, kind, subject);
    CommitChange();
    return {api::ControlStatus::Applied};
}

api::ControlReceipt GameSession::WakeProcessCarrier(NativeCaller caller, ProcessCarrierKind kind, uint64_t subject) {
    if (caller == NativeCaller::Input || kind == ProcessCarrierKind::Device)
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "PermissionDenied"};
    const auto reason = WriteRejection();
    if (reason != api::Reason::None) return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
    Formulas.WakeCarrier(kind, subject);
    CommitChange();
    return {api::ControlStatus::Applied};
}
} // namespace pixel
