#include "GameSession.h"
#include <algorithm>

namespace pixel {
namespace {
std::string_view WaitingFor(const ProcessInspection &process) {
    using namespace formula::process;
    if (process.State.Status == Phase::Sleeping) return "delay";
    if (process.State.Status != Phase::Awaiting) return "none";
    switch (process.WaitingFor) {
    case AwaitKind::Result: return "result";
    case AwaitKind::Hit: return "hit";
    case AwaitKind::Wake: return "wake";
    }
    throw std::logic_error("unknown process wait kind");
}

api::InventoryObservation ObserveContainer(const items::Inventory &inventory, items::ContainerId id,
                                           uint64_t actor, uint64_t device, api::Position location) {
    const auto source = inventory.Inspect(id);
    api::InventoryObservation result{source.Ref.Id.Value, source.Ref.Version, actor, device, location,
                                     source.Capacity, source.Quantity, {}};
    result.Stacks.reserve(source.Stacks.size());
    for (const auto &stack : source.Stacks) {
        const auto &definition = items::Definition(stack.Item);
        result.Stacks.push_back({uint32_t(stack.Item), definition.Version, stack.Quantity, stack.Reserved});
    }
    return result;
}
} // namespace

void GameSession::PublishGameplayObservation() {
    api::GameplayObservation result;
    result.Stamp = CurrentCommit();
    if (Garden.Enabled)
        result.Garden = ForceGardenObservation{Garden, SampleGarden(GardenWildX, Garden.WildGrowth),
                                                     SampleGarden(GardenHomeX, Garden.HomeGrowth)};
    if (const auto player = Characters.PlayerSnapshot())
        result.Inventories.push_back(ObserveContainer(Inventory, PlayerBag, player->Id.Value, 0,
            ActionPosition(player->GetWorldPosition())));
    for (const auto &device : Machines.Snapshot()) {
        result.Devices.push_back({device.Ref, ForceEmitterType, device.Owner, {device.X, device.Y}, device.Enabled,
                                 device.State, device.Instance, device.Cycles, device.Fuel});
        result.Inventories.push_back(ObserveContainer(Inventory, Machines.Find(device.Ref.Id)->FuelContainer,
            device.Owner, device.Ref.Id, {{device.X, device.Y}, {0, 0}}));
    }
    const auto processes = Formulas.InspectProcesses();
    for (const auto &process : processes) {
        const auto &state = process.State;
        result.Processes.push_back({state.Id, process.Subject, {process.ResourceId, process.Version},
            api::CarrierKind(process.Carrier), {{state.Origin.CellX, state.Origin.CellY},
            {state.Origin.OffsetX, state.Origin.OffsetY}}, api::ProcessPhase(state.Status), process.Suspended,
            state.ActiveSteps, state.Remaining, WaitingFor(process)});
    }
    const auto &sources = GameWorld.GetEnvironment().GetFieldSources();
    for (const auto &source : sources.Sources()) {
        api::FieldSourceObservation value;
        value.Handle = source.Handle.Id;
        value.OwnerId = source.Owner;
        value.Center = ActionPosition(source.Spec.Center);
        value.RadiusCells = source.Spec.Radius;
        value.AccelerationCellsPerSecondSquared = {source.Spec.Acceleration.X, source.Spec.Acceleration.Y};
        value.RemainingSteps = source.Remaining;
        value.Suspended = source.Suspended;
        value.Contributing = sources.Contributes(source.Handle, GameWorld.GetSimulationBounds());
        for (const auto &process : processes) {
            if (process.State.Id != source.Owner || std::find(process.ReferencedFieldHandles.begin(),
                process.ReferencedFieldHandles.end(), source.Handle.Id) == process.ReferencedFieldHandles.end()) continue;
            value.OwnerInstance = process.State.Id;
            if (process.Carrier == ProcessCarrierKind::Device) value.DeviceId = process.Subject;
            break;
        }
        result.FieldSources.push_back(value);
    }
    if (result.RetainedBytes() > api::GameplayObservation::MaximumRetainedBytes)
        throw std::logic_error("gameplay observation exceeded its fixed payload budget");
    GameplayValues = std::move(result);
}
} // namespace pixel
