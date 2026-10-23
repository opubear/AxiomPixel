#include "Gameplay/GameSession.h"
#include "DeviceCosts.h"

namespace pixel {
namespace {
api::ActionReceipt Refused(api::Reason reason) {
    api::ActionReceipt result;
    result.Rejection = reason;
    return result;
}
api::ActionReceipt Changed(DeviceSnapshot device, api::DeviceEvent event) {
    api::ActionReceipt result;
    result.Result = api::DeviceChanged{device, event};
    return result;
}
void RequireApplied(items::Result result) {
    if (result != items::Result::Applied) throw std::logic_error("preflighted device inventory operation failed");
}
} // namespace

api::ActionReceipt GameSession::PlaceDevice(api::Cell target) {
    const auto player = Characters.PlayerSnapshot();
    if (!player) return Refused(api::Reason::PermissionDenied);
    if (Machines.Overlaps(target.X, target.Y)) return Refused(api::Reason::Blocked);
    for (uint32_t y = 0; y < DeviceHeight; ++y)
        for (uint32_t x = 0; x < DeviceWidth; ++x) {
            const CellCoord cell{target.X + x, target.Y + y};
            const auto material = GameWorld.TryGetCell(cell.X, cell.Y);
            if (!material) return Refused(api::Reason::NotReady);
            if (material->Type != CellTypeId::Air || Characters.Occupies(cell)) return Refused(api::Reason::Blocked);
        }
    if (Machines.Count() == Devices::MaximumCount || Inventory.Count() == items::Inventory::MaximumContainers)
        return Refused(api::Reason::LimitExceeded);
    const auto payment = Inventory.Reserve(Inventory.Reference(PlayerBag), WindEmitterCost());
    if (payment.Status == items::Result::InsufficientItems) return Refused(api::Reason::InsufficientItems);
    if (payment.Status == items::Result::ReservationLimit) return Refused(api::Reason::LimitExceeded);
    RequireApplied(payment.Status);
    // The remaining operations cannot refuse after this single-writer preflight.
    const auto fuel = Inventory.Create(64);
    const auto ref = Machines.Create(target.X, target.Y, player->Id.Value, fuel);
    RequireApplied(Inventory.Consume(payment.Id));
    return Changed(Machines.Find(ref.Id)->Value, api::DeviceEvent::Placed);
}

api::ActionReceipt GameSession::ChangeDevice(DeviceRef ref, const api::Action &action) {
    const auto *record = Machines.Find(ref.Id);
    if (!record || record->Value.Ref != ref) return Refused(api::Reason::PreconditionFailed);
    const auto player = Characters.PlayerSnapshot();
    if (!player || record->Value.Owner != player->Id.Value) return Refused(api::Reason::PermissionDenied);
    auto device = record->Value;
    const auto fuel = record->FuelContainer;
    if (std::holds_alternative<api::RemoveDevice>(action)) {
        const auto bag = Inventory.Inspect(PlayerBag);
        const auto remaining = Inventory.Inspect(fuel);
        uint64_t refund = remaining.Quantity;
        for (const auto &amount : WindEmitterCost()) refund += amount.Quantity;
        if (refund > bag.Capacity - bag.Quantity) return Refused(api::Reason::InventoryFull);
        Formulas.CancelCarrier(ProcessCarrierKind::Device, ref.Id);
        if (remaining.Quantity) {
            const std::array stock{items::ItemAmount{items::ItemId::Coal, remaining.Quantity}};
            RequireApplied(Inventory.Transfer(remaining.Ref, Inventory.Reference(PlayerBag), stock));
        }
        RequireApplied(Inventory.Add(Inventory.Reference(PlayerBag), WindEmitterCost()));
        Inventory.Destroy(fuel);
        Machines.Remove(ref);
        device.Enabled = false;
        device.Instance = 0;
        device.State = DeviceState::Stopped;
        device.Fuel = 0;
        return Changed(device, api::DeviceEvent::Removed);
    }
    auto event = api::DeviceEvent::Enabled;
    if (const auto *supply = std::get_if<api::SupplyDevice>(&action)) {
        const std::array stock{items::ItemAmount{items::ItemId::Coal, supply->Quantity}};
        const auto result = Inventory.Transfer(Inventory.Reference(PlayerBag), Inventory.Reference(fuel), stock);
        if (result == items::Result::InsufficientItems) return Refused(api::Reason::InsufficientItems);
        if (result == items::Result::CapacityExceeded) return Refused(api::Reason::InventoryFull);
        RequireApplied(result);
        device.Fuel += supply->Quantity;
        event = api::DeviceEvent::Supplied;
    } else {
        device.Enabled = std::get<api::SetDeviceEnabled>(action).Enabled;
        if (!device.Enabled) {
            Formulas.CancelCarrier(ProcessCarrierKind::Device, ref.Id);
            device.Instance = 0;
        }
    }
    if (!device.Enabled) device.State = DeviceState::Stopped;
    else if (!device.Instance) device.State = device.Fuel ? DeviceEnvironment(device) : DeviceState::NeedsFuel;
    Machines.Update(device);
    return Changed(Machines.Find(ref.Id)->Value, event);
}
} // namespace pixel
