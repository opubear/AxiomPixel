#include "Runtime/Gameplay/GameSession/GameSession.h"
#include <algorithm>
#include <atomic>
#include <type_traits>

namespace pixel {
namespace {
// Host identities are never reused across Session objects or world replacements.
uint64_t NewIdentity() {
    static std::atomic<uint64_t> next{1};
    const auto id = next.fetch_add(1);
    if (id == UINT64_MAX)
        std::terminate();
    return id;
}
bool ValidCell(api::Cell cell) {
    // Leave the extreme chunk to internal muzzle offsets and neighbourhood reads.
    constexpr int64_t limit = (int64_t{1} << 49) - 1;
    const auto chunk = ChunkOf({cell.X, cell.Y});
    return chunk.X > -limit && chunk.X < limit && chunk.Y > -limit && chunk.Y < limit;
}
bool ValidPosition(const api::Position &position) {
    return ValidCell(position.AnchorCell) && std::all_of(position.Offset.begin(), position.Offset.end(), [](double value) { return std::isfinite(value) && value >= 0 && value < 1; });
}
WorldPosition WorldPoint(const api::Position &position) {
    const CellCoord cell{position.AnchorCell.X, position.AnchorCell.Y};
    const auto chunk = ChunkOf(cell);
    return {chunk, {double(cell.X - chunk.X * storage::ChunkEdge) + position.Offset[0], double(cell.Y - chunk.Y * storage::ChunkEdge) + position.Offset[1]}};
}
api::Reason Validate(const api::Action &action) {
    return std::visit([](const auto &value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, api::MineRegion>) {
            if (!ValidCell(value.Target) || (value.ExpectedMaterial && !IsKnownCellType(CellTypeId(*value.ExpectedMaterial))))
                return api::Reason::InvalidArgument;
        } else if constexpr (std::is_same_v<T, api::PlaceMaterial>) {
            if (!ValidCell(value.Target))
                return api::Reason::InvalidArgument;
            if (value.MaterialId != uint32_t(CellTypeId::Dirt))
                return api::Reason::Unsupported;
        } else if constexpr (std::is_same_v<T, api::SpawnElement>) {
            if (!ValidCell(value.Target) || value.Radius > 8)
                return api::Reason::InvalidArgument;
            const auto type = CellTypeId(value.MaterialId);
            if (type != CellTypeId::Water && type != CellTypeId::Steam && type != CellTypeId::Fire)
                return api::Reason::Unsupported;
        } else if constexpr (std::is_same_v<T, api::ActivateProgram>) {
            if (!ValidPosition(value.Target))
                return api::Reason::InvalidArgument;
        } else if constexpr (std::is_same_v<T, api::SelectWindStrategy>) {
            if (value.Kind < api::WindKind::Coarse || value.Kind > api::WindKind::Empty)
                return api::Reason::Unsupported;
        } else if constexpr (std::is_same_v<T, api::PlaceDevice> || std::is_same_v<T, api::ObserveForce>) {
            if (!ValidCell(value.Target))
                return api::Reason::InvalidArgument;
        } else if constexpr (std::is_same_v<T, api::RemoveDevice> || std::is_same_v<T, api::SupplyDevice> || std::is_same_v<T, api::SetDeviceEnabled> || std::is_same_v<T, api::SetDeviceProtection>) {
            if (!value.Device.Id || !value.Device.Version)
                return api::Reason::InvalidArgument;
            if constexpr (std::is_same_v<T, api::SupplyDevice>)
                if (!value.Quantity || value.Quantity > 64)
                    return api::Reason::InvalidArgument;
        } else if constexpr (std::is_same_v<T, api::InstallIntervention>) {
            if (!value.ExpectedResourceVersion || !authority::Control::ValidRegion(value.Scope) || !ValidCell(value.Origin)
                || !authority::Control::Contains(value.Scope, value.Origin))
                return api::Reason::InvalidArgument;
        } else if constexpr (std::is_same_v<T, api::InterveneAtDevice>) {
            if (!value.ExpectedResourceVersion || !value.Device.Id || !value.Device.Version)
                return api::Reason::InvalidArgument;
        } else if constexpr (std::is_same_v<T, api::SetWindBoundary>) {
            if (!ValidCell({0, value.Y}))
                return api::Reason::InvalidArgument;
        }
        return api::Reason::None;
    },
                      action);
}
WindStrategyKind WorldWind(api::WindKind kind) {
    switch (kind) {
    case api::WindKind::Coarse:
        return WindStrategyKind::Coarse;
    case api::WindKind::Reference:
        return WindStrategyKind::Reference;
    case api::WindKind::Procedural:
        return WindStrategyKind::Procedural;
    case api::WindKind::Hybrid:
        return WindStrategyKind::Hybrid;
    case api::WindKind::Adaptive:
        return WindStrategyKind::Adaptive;
    case api::WindKind::Lbm:
        return WindStrategyKind::Lbm;
    case api::WindKind::Empty:
        return WindStrategyKind::Empty;
    }
    throw std::logic_error("unvalidated wind kind");
}
api::WindKind PublicWind(WindStrategyKind kind) {
    switch (kind) {
    case WindStrategyKind::Coarse:
        return api::WindKind::Coarse;
    case WindStrategyKind::Reference:
        return api::WindKind::Reference;
    case WindStrategyKind::Procedural:
        return api::WindKind::Procedural;
    case WindStrategyKind::Hybrid:
        return api::WindKind::Hybrid;
    case WindStrategyKind::Adaptive:
        return api::WindKind::Adaptive;
    case WindStrategyKind::Lbm:
        return api::WindKind::Lbm;
    case WindStrategyKind::Empty:
        return api::WindKind::Empty;
    }
    throw std::logic_error("unknown internal wind kind");
}
} // namespace

void GameSession::RenewActionSessions() {
    Epoch = NewIdentity();
    for (size_t i = 0; i < Callers.size(); ++i) {
        Callers[i] = {};
        Callers[i].Id = NewIdentity();
        Callers[i].Debug = i == size_t(NativeCaller::Debug) || i == size_t(NativeCaller::Driver);
    }
    Authority.Reset(WorldWillSession());
    Interventions.clear();
    NextIntervention = 1;
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
        if (state.Id == caller && caller != 0)
            return &state;
    return nullptr;
}
api::Reason GameSession::WriteRejection() const {
    if (!Ready)
        return api::Reason::SessionUnavailable;
    if (GameWorld.GetEnvironment().IsWaitingForBudget())
        return api::Reason::WaitingForBudget;
    if (Paused)
        return api::Reason::GamePaused;
    return api::Reason::None;
}
api::Reason GameSession::PrepareWorldMutation() {
    const auto reason = WriteRejection();
    if (reason != api::Reason::None) return reason;
    return GameWorld.TryPrepareBuffers() ? api::Reason::None : api::Reason::WaitingForBudget;
}
api::ActionReceipt GameSession::Execute(NativeCaller caller, api::Action action) {
    const auto id = CallerSession(caller);
    const auto high = FindCaller(id)->HighWatermark;
    if (high == UINT64_MAX)
        throw std::overflow_error("action sequence exhausted");
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
    if (!state)
        return reject(api::Reason::CallerExpired);
    if (request.WorldEpoch != Epoch)
        return reject(api::Reason::StaleEpoch);
    if (request.Sequence == 0)
        return reject(api::Reason::InvalidArgument);
    if (request.Sequence <= state->HighWatermark) {
        for (const auto &previous : state->Receipts)
            if (previous.Key.Sequence == request.Sequence)
                return previous;
        return reject(api::Reason::DuplicateExpired);
    }

    api::Reason reason = request.SchemaVersion != 1
                           ? api::Reason::InvalidArgument
                           : Validate(request.Payload);
    if (reason == api::Reason::None)
        reason = PrepareWorldMutation();
    if (reason == api::Reason::None)
        reason = AuthorizeAction(caller, request.Payload);
    const bool wind = std::holds_alternative<api::SelectWindStrategy>(request.Payload) || std::holds_alternative<api::SetWindBoundary>(request.Payload);
    if (reason == api::Reason::None && (wind || std::holds_alternative<api::SpawnElement>(request.Payload)) && !state->Debug)
        reason = api::Reason::PermissionDenied;
    if (reason != api::Reason::None) {
        receipt = reject(reason);
    } else {
        const api::WindConfiguration beforeWind{PublicWind(WindKind), GameWorld.GetWindBoundary()};
        receipt = Dispatch(request.Payload, {request.WorldEpoch, caller, request.Sequence});
        receipt.Key = {request.WorldEpoch, caller, request.Sequence};
        if (receipt.Applied()) {
            Simulation.CommitChange();
            // Preserve initial process effects before cancellation clears the bridge log.
            RecordActionFacts(receipt, beforeWind);
            const auto authorityChanges = ReconcileAuthority();
            RecordAuthorityFacts(authorityChanges);
            receipt.Facts.Count = uint32_t(Facts.HighWatermark() - receipt.Facts.FirstSequence + 1);
            PublishCommit();
        }
        receipt.ResolvedAt = CurrentCommit();
    }
    // Terminal refusals also consume sequence numbers. Storage stays bounded.
    state->HighWatermark = std::max(state->HighWatermark, request.Sequence);
    if (state->Receipts.size() == 64)
        state->Receipts.pop_front();
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
                        case MiningStatus::Applied:
                            result.Collected = mined.Yield;
                            break;
                        case MiningStatus::Blocked:
                            receipt.Rejection = api::Reason::Blocked;
                            break;
                        case MiningStatus::InventoryFull:
                            receipt.Rejection = api::Reason::InventoryFull;
                            break;
                        case MiningStatus::NotReady:
                            receipt.Rejection = api::Reason::WaitingForBudget;
                            break;
                        }
                    } else {
                        if (before->Type != CellTypeId::Air || Characters.Occupies({value.Target.X, value.Target.Y})
                            || Machines.FindAt(value.Target.X, value.Target.Y)) {
                            receipt.Rejection = api::Reason::Blocked;
                        } else if (!GameWorld.TryPrepareMaterialWrite({value.Target.X, value.Target.Y, 1, 1})) {
                            receipt.Rejection = api::Reason::WaitingForBudget;
                        } else {
                            const std::array cost{items::ItemAmount{items::ItemId::Soil, 1}};
                            const auto reserved = Inventory.Reserve(Inventory.Reference(PlayerBag), cost);
                            if (reserved.Status == items::Result::InsufficientItems)
                                receipt.Rejection = api::Reason::InsufficientItems;
                            else if (reserved.Status == items::Result::ReservationLimit)
                                receipt.Rejection = api::Reason::LimitExceeded;
                            else {
                                if (reserved.Status != items::Result::Applied || GameWorld.TryPlaceDirt(value.Target.X, value.Target.Y) != CellEditResult::Applied || Inventory.Consume(reserved.Id) != items::Result::Applied)
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
        } else if constexpr (std::is_same_v<T, api::SpawnElement>) {
            api::ElementsSpawned result{value.Target, value.MaterialId, value.Radius};
            const int radius = int(value.Radius);
            if (!GameWorld.TryPrepareMaterialWrite({value.Target.X - radius, value.Target.Y - radius,
                                                    uint32_t(radius * 2 + 1), uint32_t(radius * 2 + 1)})) {
                receipt.Rejection = api::Reason::WaitingForBudget;
                receipt.Result = result;
                return;
            }
            bool resident = false;
            for (int y = -radius; y <= radius; ++y) {
                for (int x = -radius; x <= radius; ++x) {
                    if (x * x + y * y > radius * radius)
                        continue;
                    const CellCoord cell{value.Target.X + x, value.Target.Y + y};
                    if (!GameWorld.GetSimulationBounds().Contains(cell))
                        continue;
                    const auto before = GameWorld.TryGetCell(cell.X, cell.Y);
                    resident |= before.has_value();
                    if (!before || before->Type != CellTypeId::Air || Machines.FindAt(cell.X, cell.Y))
                        continue;
                    if (GameWorld.TryPlaceMaterial(cell, CellTypeId(value.MaterialId)) != CellEditResult::Applied)
                        throw std::logic_error("preflighted element placement failed");
                    ++result.Changed;
                }
            }
            if (!result.Changed)
                receipt.Rejection = resident ? api::Reason::Blocked : api::Reason::NotReady;
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
        } else if constexpr (std::is_same_v<T, api::InstallIntervention>) {
            receipt = InstallIntervention(value, request);
        } else if constexpr (std::is_same_v<T, api::InterveneAtDevice>) {
            receipt = InterveneAtDevice(value, request);
        } else if constexpr (std::is_same_v<T, api::SetDeviceProtection>) {
            receipt = SetDeviceProtection(value);
        } else if constexpr (std::is_same_v<T, api::ObserveForce>) {
            const auto player = Characters.PlayerSnapshot();
            const CellCoord cell{value.Target.X, value.Target.Y};
            if (!player || !GameWorld.GetSimulationBounds().Contains(cell) || !GameWorld.TryGetCell(cell.X, cell.Y)) {
                receipt.Rejection = api::Reason::NotReady;
            } else {
                const auto position = player->GetWorldPosition().RelativeTo(cell);
                // Native measurement tool: proximity authorizes observation,
                // not program inspection, copying, editing or device control.
                if (std::hypot(position[0], position[1]) > 32)
                    receipt.Rejection = api::Reason::PermissionDenied;
                else if (!Knowledge.CanDiscover(player->Id.Value, value.Target))
                    receipt.Rejection = api::Reason::LimitExceeded;
                else {
                    const auto force = GameWorld.GetForce(cell.X, cell.Y);
                    receipt.Result = api::ForceObserved{player->Id.Value, value.Target, {force.X, force.Y}};
                }
            }
        } else if constexpr (std::is_same_v<T, api::PlaceDevice>) {
            receipt = PlaceDevice(value.Target);
        } else if constexpr (std::is_same_v<T, api::RemoveDevice> || std::is_same_v<T, api::SupplyDevice> || std::is_same_v<T, api::SetDeviceEnabled>) {
            receipt = ChangeDevice(value.Device, action);
        } else {
            if constexpr (std::is_same_v<T, api::SelectWindStrategy>)
                SelectWindStrategy(WorldWind(value.Kind));
            else
                SetWindBoundary(value.Y);
            receipt.Result = api::WindResult{PublicWind(WindKind), GameWorld.GetWindBoundary()};
        }
    },
               action);
    receipt.Status = receipt.Rejection == api::Reason::None ? api::ActionStatus::Applied : api::ActionStatus::Rejected;
    if (receipt.Message.empty() && receipt.Rejection != api::Reason::None)
        receipt.Message = api::ReasonName(receipt.Rejection);
    return receipt;
}

api::ControlReceipt GameSession::Control(api::CallerSessionId caller, const api::SessionControl &control) {
    const auto *state = FindCaller(caller);
    if (!state)
        return {api::ControlStatus::Rejected, api::Reason::CallerExpired, "CallerExpired"};
    if (caller == WorldWillSession() || (!state->Debug && !std::holds_alternative<api::ResetWorld>(control)))
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "PermissionDenied"};
    auto result = std::visit([&](const auto &value) -> api::ControlReceipt {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, api::ClearRunning>) {
            const auto reason = PrepareWorldMutation();
            if (reason != api::Reason::None)
                return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
            const bool changed = Formulas.Stats().Roots != 0;
            const bool crystalStopped = Garden.Enabled && Garden.CrystalEnabled;
            if (crystalStopped)
                Garden.CrystalEnabled = false;
            Formulas.Clear(GameWorld);
            const auto stopped = StopDevices();
            const auto authorityChanges = ReconcileAuthority();
            if (changed || crystalStopped || !stopped.empty()) {
                Simulation.CommitChange();
                RecordAuthorityFacts(authorityChanges);
                for (const auto &change : stopped)
                    RecordFact(CurrentCommit(), api::DeviceCause{change.Device.Ref.Id, 0}, change);
                PublishCommit();
            }
        } else if constexpr (std::is_same_v<T, api::SaveWorld> || std::is_same_v<T, api::ResetWorld>
                             || std::is_same_v<T, api::CreateForceGarden> || std::is_same_v<T, api::RecoverWorld>) {
            if constexpr (!std::is_same_v<T, api::RecoverWorld>) {
                if (!Ready)
                    return {api::ControlStatus::Rejected, api::Reason::SessionUnavailable, "SessionUnavailable"};
                if (Simulation.HasPendingStep())
                    return {api::ControlStatus::Deferred, api::Reason::WaitingForBudget,
                            "Save/reset deferred: simulation is waiting for storage budget. The session stays open; resolve the budget shortage, finish the step, then retry."};
            }
            if constexpr (std::is_same_v<T, api::CreateForceGarden>) {
                if (Store && StreamConfiguration.WindowEdge < 128)
                    return {api::ControlStatus::Rejected, api::Reason::LimitExceeded,
                            "Force garden requires a simulation window of at least 128 cells."};
            }
            const bool success = [&] {
                if constexpr (std::is_same_v<T, api::SaveWorld>)
                    return TrySave();
                else if constexpr (std::is_same_v<T, api::ResetWorld>)
                    return TryReset();
                else if constexpr (std::is_same_v<T, api::CreateForceGarden>) {
                    CreateGardenOnReset = true;
                    const bool created = TryReset();
                    CreateGardenOnReset = false;
                    return created;
                } else
                    return TryRecover();
            }();
            if (!success)
                return {api::ControlStatus::Failed, api::Reason::StorageFailure, SaveError};
            if constexpr (std::is_same_v<T, api::SaveWorld>)
                return {api::ControlStatus::Saved};
            if constexpr (std::is_same_v<T, api::ResetWorld> || std::is_same_v<T, api::CreateForceGarden>)
                return {api::ControlStatus::Replaced};
        } else if constexpr (std::is_same_v<T, api::SetNextGenerator>) {
            if (value.Kind != api::GeneratorKind::Flat && value.Kind != api::GeneratorKind::Natural)
                return {api::ControlStatus::Rejected, api::Reason::Unsupported, "Unsupported"};
            NextGeneration.Kind = value.Kind == api::GeneratorKind::Flat ? WorldGeneratorKind::Flat : WorldGeneratorKind::Natural;
        } else if constexpr (std::is_same_v<T, api::SetNextSeed>) {
            NextGeneration.Seed = value.Seed;
        } else {
            if (!Ready)
                return {api::ControlStatus::Rejected, api::Reason::SessionUnavailable, "SessionUnavailable"};
            if constexpr (std::is_same_v<T, api::SetPaused>) {
                if (value.Paused != Paused) {
                    if (Simulation.HasPendingStep())
                        return {api::ControlStatus::Rejected, api::Reason::WaitingForBudget, "WaitingForBudget"};
                    Paused = value.Paused;
                    Simulation.ClearInput();
                    CommitChange();
                }
            } else if constexpr (std::is_same_v<T, api::StepSimulation>)
                Simulation.RequestStep();
            else if constexpr (std::is_same_v<T, api::RetryStreaming>)
                GameWorld.RetryStreaming();
        }
        return {api::ControlStatus::Applied};
    },
                             control);
    if (std::holds_alternative<api::SaveWorld>(control) || std::holds_alternative<api::ResetWorld>(control)
        || std::holds_alternative<api::CreateForceGarden>(control) || std::holds_alternative<api::RecoverWorld>(control))
        PersistenceResult = result;
    return result;
}

ApplyProgramResult GameSession::ApplyProcess(NativeCaller caller, const formula::process::ProgramArtifact &program) {
    if (caller == NativeCaller::Input)
        return {api::Reason::PermissionDenied, {}, "PermissionDenied"};
    const auto reason = PrepareWorldMutation();
    if (reason != api::Reason::None)
        return {reason, {}, api::ReasonName(reason)};
    Formulas.ApplyProcess(program);
    CommitChange();
    return {api::Reason::None, AppliedProgram(), {}};
}

api::ControlReceipt GameSession::StartProcessFixture(NativeCaller caller, const ProcessCarrier &carrier) {
    if (caller == NativeCaller::Input || carrier.Kind == ProcessCarrierKind::Player || carrier.Kind == ProcessCarrierKind::Device || carrier.Kind == ProcessCarrierKind::Intervention)
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "Fixture requires native host assembly"};
    const auto reason = PrepareWorldMutation();
    if (reason != api::Reason::None)
        return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
    if (!Formulas.CastCarrier(GameWorld, carrier))
        return {api::ControlStatus::Rejected, api::Reason::InvalidProgram, Formulas.LastError()};
    Simulation.CommitChange();
    const api::ProgramEffectCause cause{{Epoch, Formulas.LastActivationId()}, AppliedProgram(), 0, {}, {}};
    RecordFact(CurrentCommit(), cause, api::ProgramActivated{AppliedProgram(), Formulas.LastActivationId()});
    RecordFormulaFacts();
    PublishCommit();
    return {api::ControlStatus::Applied};
}

api::ControlReceipt GameSession::CancelProcessCarrier(NativeCaller caller, ProcessCarrierKind kind, uint64_t subject) {
    if (caller == NativeCaller::Input || kind == ProcessCarrierKind::Device)
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "PermissionDenied"};
    const auto reason = PrepareWorldMutation();
    if (reason != api::Reason::None)
        return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
    Formulas.CancelCarrier(GameWorld, kind, subject);
    const auto authorityChanges = ReconcileAuthority();
    Simulation.CommitChange();
    RecordAuthorityFacts(authorityChanges);
    PublishCommit();
    return {api::ControlStatus::Applied};
}

api::ControlReceipt GameSession::WakeProcessCarrier(NativeCaller caller, ProcessCarrierKind kind, uint64_t subject) {
    if (caller == NativeCaller::Input || kind == ProcessCarrierKind::Device)
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "PermissionDenied"};
    const auto reason = PrepareWorldMutation();
    if (reason != api::Reason::None)
        return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
    Formulas.WakeCarrier(kind, subject);
    CommitChange();
    return {api::ControlStatus::Applied};
}
} // namespace pixel
