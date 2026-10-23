#pragma once
#include "Actions.h"
#include "Ecology/ForceGarden.h"
#include <string_view>
#include <vector>

namespace pixel::api {
// Public behavior and owned inventory values only. No program source, locals,
// instruction pointer or mutable domain references enter this contract.
struct ItemStackObservation {
    uint32_t ItemId = 0, DefinitionVersion = 0, Quantity = 0, Reserved = 0;
};
struct InventoryObservation {
    uint64_t ContainerId = 0, Version = 0, OwnerActor = 0, DeviceId = 0;
    Position Location;
    uint32_t Capacity = 0, Quantity = 0;
    std::vector<ItemStackObservation> Stacks;
    uint64_t RetainedBytes() const { return sizeof(*this) + Stacks.capacity() * sizeof(ItemStackObservation); }
};
struct DeviceObservation {
    DeviceRef Ref;
    uint32_t TypeId = ForceEmitterType;
    uint64_t OwnerActor = 0;
    Cell Anchor;
    bool Enabled = false;
    DeviceState State = DeviceState::Stopped;
    uint64_t InstanceId = 0, Cycles = 0;
    // Absent when the observer lacks this owner's inventory grant.
    std::optional<uint32_t> Fuel;
};
std::string_view DeviceStateReason(DeviceState state);

enum class CarrierKind { Player, NaturalSource, FixedFixture, Device };
enum class ProcessPhase { Ready, Sleeping, Awaiting, Ended };
struct ProcessObservation {
    uint64_t InstanceId = 0, Subject = 0;
    ProgramRef Program;
    CarrierKind Carrier = CarrierKind::Player;
    Position Location;
    ProcessPhase Phase = ProcessPhase::Ready;
    bool Suspended = false;
    uint64_t ActiveSteps = 0, RemainingSteps = 0;
    std::string_view WaitingFor = "none"; // none, delay, result, hit, wake; observed scheduling only.
};
struct FieldSourceObservation {
    uint64_t Handle = 0, OwnerId = 0, OwnerInstance = 0, DeviceId = 0;
    Position Center;
    double RadiusCells = 0;
    std::array<double, 2> AccelerationCellsPerSecondSquared{};
    uint64_t RemainingSteps = 0;
    bool Suspended = false;
    bool Contributing = false;
};

struct GameplayObservation {
    static constexpr uint64_t MaximumRetainedBytes = 128 * 1024;
    CommitStamp Stamp;
    std::vector<InventoryObservation> Inventories;
    std::vector<DeviceObservation> Devices;
    std::vector<ProcessObservation> Processes;
    std::vector<FieldSourceObservation> FieldSources;
    std::optional<ForceGardenObservation> Garden;
    uint64_t RetainedBytes() const;
};
} // namespace pixel::api
