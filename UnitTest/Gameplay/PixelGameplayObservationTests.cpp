#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/FormulaBridge/ProcessHost.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

api::ObservationSnapshot Observe(GameSession &session, api::Rect scope = {{480, 464}, 64, 64}) {
    auto result = session.Observe(NativeCaller::Driver, scope);
    Check(result.Status == api::ObservationStatus::Available && result.Snapshot.has_value(), "observation unavailable");
    Check(result.Snapshot->Gameplay.Stamp == result.Snapshot->Stamp, "mixed domain commit");
    return std::move(*result.Snapshot);
}

void Step(GameSession &session) {
    session.BeginFrame();
    Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "step rejected");
    session.Tick(1.f / 60, {});
}

DeviceSnapshot Place(GameSession &session) {
    auto &inventory = testing::SessionAccess::Inventory(session);
    const std::array stock{items::ItemAmount{items::ItemId::Stone, 4}, items::ItemAmount{items::ItemId::CopperOre, 2},
                           items::ItemAmount{items::ItemId::Coal, 3}};
    Check(inventory.Add(inventory.Reference(session.PlayerInventory()), stock) == items::Result::Applied, "stock failed");
    const auto placed = session.Execute(NativeCaller::Driver, api::PlaceDevice{{500, 470}});
    Check(placed.Applied(), "placement failed");
    return std::get<api::DeviceChanged>(placed.Result).Device;
}

void SharedCommitAndDomainLifecycle() {
    GameSession session;
    const auto view = session.CreateReadView();
    session.RefreshReadView(*view, {448, 448, 128, 128});
    auto device = Place(session);
    auto placed = Observe(session);
    Check(placed.Gameplay.Devices.size() == 1 && placed.Gameplay.Inventories.size() == 2
        && placed.Gameplay.Devices[0].Fuel == 0 && placed.Gameplay.Processes.empty(), "placed domains incomplete");
    Check(placed.Stamp == view->Stamp() && view->Gameplay().Stamp == placed.Stamp
        && view->GetDevices()[0].State == placed.Gameplay.Devices[0].State, "visual and observation facts differ");
    Check(api::DeviceStateReason(placed.Gameplay.Devices[0].State) == "disabled", "stopped reason missing");

    Check(session.Execute(NativeCaller::Driver, api::SetDeviceEnabled{device.Ref, true}).Applied(), "enable failed");
    Step(session);
    const auto empty = Observe(session);
    Check(empty.Gameplay.Devices[0].State == DeviceState::NeedsFuel
        && api::DeviceStateReason(empty.Gameplay.Devices[0].State) == "needs_coal", "fuel bottleneck not projected");
    device = session.GetDevices().Find(device.Ref.Id)->Value;
    Check(session.Execute(NativeCaller::Driver, api::SupplyDevice{device.Ref, 2}).Applied(), "supply failed");
    Step(session);
    auto active = Observe(session);
    Check(active.Gameplay.Processes.size() == 1 && active.Gameplay.FieldSources.size() == 1, "running domains missing");
    const auto &process = active.Gameplay.Processes.front();
    const auto &source = active.Gameplay.FieldSources.front();
    Check(process.InstanceId == active.Gameplay.Devices[0].InstanceId && source.OwnerInstance == process.InstanceId
        && source.DeviceId == device.Ref.Id && source.Contributing && !source.Suspended
        && source.AccelerationCellsPerSecondSquared == std::array<double, 2>{3600, 0}, "effect ownership or units diverged");
    Check(active.Gameplay.Devices[0].Fuel == 1 && active.Gameplay.Devices[0].Cycles == 1
        && process.Phase == api::ProcessPhase::Awaiting && process.WaitingFor == "result", "paid waiting state not projected");
    const auto activeStamp = active.Stamp;
    Step(session);
    const auto waiting = Observe(session);
    Check(waiting.Gameplay.Processes[0].Phase == api::ProcessPhase::Sleeping
        && waiting.Gameplay.Processes[0].RemainingSteps == 29
        && waiting.Gameplay.Processes[0].WaitingFor == "delay"
        && waiting.Gameplay.FieldSources[0].RemainingSteps < source.RemainingSteps, "continuation progress not visible");
    Check(active.Stamp == activeStamp && active.Gameplay.Processes[0].Phase == api::ProcessPhase::Awaiting,
        "previous observation borrowed live process state");

    auto &world = testing::SessionAccess::World(session);
    world.SetCell(500, 470, {CellTypeId::Stone});
    testing::SessionAccess::PublishFixtureChanges(session);
    const auto blocked = Observe(session);
    Check(blocked.Gameplay.Devices[0].State == DeviceState::WaitingForEnvironment
        && blocked.Gameplay.Processes.empty() && blocked.Gameplay.FieldSources.empty(), "blocked projection retained effects");
    Check(api::DeviceStateReason(blocked.Gameplay.Devices[0].State) == "device_footprint_blocked", "blocked reason missing");
    device = session.GetDevices().Find(device.Ref.Id)->Value;
    Check(session.Execute(NativeCaller::Driver, api::RemoveDevice{device.Ref}).Applied(), "recycle failed");
    const auto removed = Observe(session);
    Check(removed.Gameplay.Devices.empty() && removed.Gameplay.Inventories.size() == 1
        && view->Gameplay().Devices.empty() && view->GetDevices().empty(), "deleted device or container remained visible");
    Check(active.Gameplay.Devices.size() == 1 && active.Gameplay.FieldSources.size() == 1, "frozen values changed after delete");
    Check(removed.RetainedBytes() <= ObservationProjector::MaximumSnapshotBytes
        && session.ObservationStats().RetainedBytes <= session.ObservationStats().MaximumRetainedBytes,
        "domain projection exceeded memory contract");
}

void ScopeAndNativeOwnership() {
    GameSession session;
    auto device = Place(session);
    auto &world = testing::SessionAccess::World(session);
    const auto source = world.InstallFieldSource(999, {WorldPosition::FromAbsolute(501.25, 471.5), 3, {120, -240}, 10});
    Check(source.has_value(), "native source failed");
    testing::SessionAccess::PublishFixtureChanges(session);
    const auto values = Observe(session);
    Check(values.Gameplay.FieldSources[0].OwnerId == 999 && !values.Gameplay.FieldSources[0].OwnerInstance
        && !values.Gameplay.FieldSources[0].DeviceId && values.Gameplay.FieldSources[0].Contributing
        && values.Gameplay.FieldSources[0].Center.Offset == std::array<double, 2>{.25, .5}, "native source misidentified");
    const auto remote = Observe(session, {{0, 0}, 64, 64});
    Check(remote.Gameplay.Inventories.empty() && remote.Gameplay.Devices.empty() && remote.Gameplay.FieldSources.empty(),
        "spatial capture exported distant domain objects");
    world.SuspendFieldSources(999, true);
    testing::SessionAccess::PublishFixtureChanges(session);
    const auto frozen = Observe(session);
    Check(frozen.Gameplay.FieldSources[0].Suspended && !frozen.Gameplay.FieldSources[0].Contributing,
        "suspended source claimed contribution");
    Check(session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded(), "reset failed");
    const auto reset = Observe(session);
    Check(reset.Stamp.WorldEpoch != values.Stamp.WorldEpoch && reset.Gameplay.Devices.empty()
        && reset.Gameplay.FieldSources.empty() && reset.Gameplay.Inventories.size() == 1
        && reset.Gameplay.Inventories[0].Quantity == 0, "epoch replacement retained gameplay values");
}

void FullDomainsFitPublicationBudget() {
    api::GameplayObservation values;
    values.Stamp = {std::string(128, 'w'), 1, 100, 100};
    api::WorldWillMemory memory;
    memory.NextPrayer = api::WorldWillMemory::Capacity + 1;
    for (size_t i = 0; i < api::WorldWillMemory::Capacity; ++i) {
        api::PrayerRecord prayer;
        prayer.Id = i + 1;
        prayer.State = api::DecisionState::NoAction;
        prayer.ObservedAt = prayer.ResolvedAt = values.Stamp;
        prayer.Prayer.assign(api::WorldWillMemory::PrayerBytes, 'p');
        prayer.Explanation.assign(api::WorldWillMemory::ExplanationBytes, 'e');
        memory.Records.push_back(std::move(prayer));
    }
    values.WorldWill = memory;

    // Match publication growth, including the 65th container expanding the
    // vector. Counting only element sizes misses this legal combined boundary.
    api::InventoryObservation player;
    player.Stacks.reserve(items::Definitions().size());
    for (const auto &item : items::Definitions()) player.Stacks.push_back({uint32_t(item.Id), item.Version, 1, 0});
    values.Inventories.push_back(std::move(player));
    for (size_t i = 0; i < Devices::MaximumCount; ++i) {
        values.Devices.push_back({});
        api::InventoryObservation fuel;
        const auto &coal = items::Definition(items::ItemId::Coal);
        fuel.Stacks.push_back({uint32_t(coal.Id), coal.Version, 1, 0});
        values.Inventories.push_back(std::move(fuel));
        values.Protections.push_back({i + 1, {{0, 0}, 34, 34}, false});
    }
    for (size_t i = 0; i < ProcessHost::MaximumInstances; ++i) values.Processes.push_back({});
    for (size_t i = 0; i < FieldSources::Capacity; ++i) values.FieldSources.push_back({});

    const auto bytes = values.RetainedBytes();
    Check(bytes <= api::GameplayObservation::MaximumRetainedBytes,
        "legal full prayer archive and gameplay domains exceed publication budget");
    std::cout << "Maximum gameplay observation: " << bytes << " / "
        << api::GameplayObservation::MaximumRetainedBytes << " retained bytes\n";
}
} // namespace

int main() {
    try {
        SharedCommitAndDomainLifecycle();
        ScopeAndNativeOwnership();
        FullDomainsFitPublicationBudget();
        std::cout << "Committed gameplay observation contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
