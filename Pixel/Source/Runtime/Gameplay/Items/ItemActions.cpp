#include "HandheldItem.h"
#include "Runtime/Gameplay/GameSession/GameSession.h"
#include "Runtime/Gameplay/FormulaBridge/DefaultProcess.h"
#include <algorithm>
#include <cmath>

namespace pixel {
api::ActionReceipt GameSession::UseHeldItem(const api::UseHeldItem &action, api::RequestKey request) {
    api::ActionReceipt result;
    if (!action.Item || !Inventory.ContainsItem(PlayerBag, *action.Item)) {
        result.Rejection = api::Reason::InsufficientItems;
        return result;
    }
    if (!GetPlayer()) {
        result.Rejection = api::Reason::NotReady;
        return result;
    }
    const auto target = WorldPosition::FromCell({action.Target.AnchorCell.X, action.Target.AnchorCell.Y}, action.Target.Offset);
    const items::ItemUseContext context{
        [&](uint32_t material) {
            const auto &body = *GetPlayer();
            const auto origin = body.GetWorldPosition();
            const auto reference = origin.Cell();
            const auto player = origin.RelativeTo(reference), aim = target.RelativeTo(reference);
            const bool left = aim[0] < player[0] + body.Size[0] * .5f;
            // Choose the first complete cell outside the body. A half-cell
            // offset can still intersect it when the player stands between cells.
            const auto cellOrigin = body.CellOrigin();
            const double edge = double(body.X + (left ? 0.f : body.Size[0])) / CentimetersPerCell;
            const CellCoord cell{
                cellOrigin.X + int64_t(left ? std::floor(edge) - 1 : std::ceil(edge)),
                cellOrigin.Y + int64_t(std::floor(double(body.Y + body.Size[1] * .5f) / CentimetersPerCell))};
            return Dispatch(api::PlaceMaterial{{cell.X, cell.Y}, material}, request);
        },
        [&](const items::ItemInstance &item) {
            api::ActionReceipt cast;
            cast.Rejection = ActivateAt(target, request, &item);
            cast.Message = FormulaError;
            if (cast.Rejection == api::Reason::None) {
                const auto stats = Formulas.Stats();
                cast.Result = api::ActivationResult{Epoch, Formulas.LastActivationId(), items::ItemProgram(item),
                                                    stats.Roots, stats.Projectiles, stats.Pending};
            }
            return cast;
        }};
    return items::MakeHandheldItem(Inventory, PlayerBag, *action.Item)->Use(context);
}

api::ControlReceipt GameSession::DropWoodenStick(NativeCaller caller) {
    if (Worker && !Worker->IsCurrent())
        return Worker->Invoke([&] { return DropWoodenStick(caller); });
    if (caller == NativeCaller::Input)
        return {api::ControlStatus::Rejected, api::Reason::PermissionDenied, "Debug command only"};
    const auto reason = PrepareWorldMutation();
    if (reason != api::Reason::None)
        return {api::ControlStatus::Rejected, reason, api::ReasonName(reason)};
    if (!GetPlayer()) return {api::ControlStatus::Rejected, api::Reason::NotReady, "Player unavailable"};

    auto position = GetPlayer()->GetWorldPosition();
    position.Local[0] += GetPlayer()->Size[0] + CellsToCentimeters(4.5f);
    position.Local[1] += GetPlayer()->Size[1] - CellsToCentimeters(.5f);
    position.Normalize();
    const auto cell = position.Cell();
    const auto material = GameWorld.TryGetCell(cell.X, cell.Y);
    if (!GameWorld.GetSimulationBounds().Contains(cell) || !material)
        return {api::ControlStatus::Rejected, api::Reason::NotReady, "Drop position is not loaded"};
    if (material->Type != CellTypeId::Air || Machines.FindAt(cell.X, cell.Y))
        return {api::ControlStatus::Rejected, api::Reason::Blocked, "Drop position is blocked"};

    items::ItemRef item;
    const auto created = Inventory.CreateStick(Inventory.Reference(GroundBag), DefaultPlayerProcess(), item);
    if (created == items::Result::CapacityExceeded)
        return {api::ControlStatus::Rejected, api::Reason::LimitExceeded, "Too many dropped items"};
    if (created != items::Result::Applied) throw std::logic_error("invalid ground inventory");
    DroppedItems.push_back({item, position});
    CommitChange();
    return {api::ControlStatus::Applied};
}

void GameSession::PickUpNearbyItems() {
    if (!GetPlayer() || DroppedItems.empty()) return;
    const auto &body = *GetPlayer();
    const auto origin = body.GetWorldPosition();
    const auto reference = origin.Cell();
    const auto player = origin.RelativeTo(reference);
    auto center = origin;
    center.Local[0] += body.Size[0] * .5f;
    center.Local[1] += body.Size[1] * .5f;
    center.Normalize();
    for (auto it = DroppedItems.begin(); it != DroppedItems.end();) {
        const auto point = it->Position.RelativeTo(reference);
        const float dx = point[0] - std::clamp(point[0], player[0], player[0] + body.Size[0]);
        const float dy = point[1] - std::clamp(point[1], player[1], player[1] + body.Size[1]);
        if (std::hypot(dx, dy) > CellsToCentimeters(2.f)
            || FormulaWorldAdapter::Sweep(GameWorld, center, it->Position).Kind != FormulaContactKind::None) {
            ++it;
            continue;
        }
        const auto transfer = Inventory.TransferInstance(Inventory.Reference(GroundBag), Inventory.Reference(PlayerBag), it->Item);
        if (transfer == items::Result::CapacityExceeded) { ++it; continue; }
        if (transfer != items::Result::Applied) throw std::logic_error("dropped item is missing from its inventory");
        it = DroppedItems.erase(it);
    }
}

ApplyProgramResult GameSession::ApplyHeldProcess(NativeCaller caller, items::ItemRef item,
                                                const formula::process::ProgramArtifact &program) {
    if (Worker && !Worker->IsCurrent())
        return Worker->Invoke([&] { return ApplyHeldProcess(caller, item, program); });
    if (caller == NativeCaller::Input) return {api::Reason::PermissionDenied, {}, "PermissionDenied"};
    const auto reason = PrepareWorldMutation();
    if (reason != api::Reason::None) return {reason, {}, api::ReasonName(reason)};
    if (!Inventory.FindInstance(PlayerBag, item))
        return {api::Reason::InsufficientItems, {}, "Hold a wooden stick to edit its formula"};
    if (Inventory.SetFormula(Inventory.Reference(PlayerBag), item, program) != items::Result::Applied)
        throw std::logic_error("owned stick could not receive its formula");
    CommitChange();
    return {api::Reason::None, items::ItemProgram(*Inventory.FindInstance(PlayerBag, item)), {}};
}
} // namespace pixel
