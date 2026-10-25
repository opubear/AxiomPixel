#pragma once
#include "Facts.h"
#include "Runtime/Gameplay/Items/Item.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace pixel::api {
enum class GeneratorKind { Flat, Natural };
struct MineRegion { Cell Target; std::optional<uint32_t> ExpectedMaterial; };
struct PlaceMaterial { Cell Target; uint32_t MaterialId = 10; };
struct ActivateProgram { ProgramRef Program; Position Target; };
struct SelectWindStrategy { WindKind Kind; };
struct SetWindBoundary { int64_t Y; };
struct PlaceDevice { Cell Target; };
struct RemoveDevice { DeviceRef Device; };
struct SupplyDevice { DeviceRef Device; uint32_t Quantity = 1; };
struct SetDeviceEnabled { DeviceRef Device; bool Enabled; };
struct ObserveForce { Cell Target; };
// Protection is a native device module. Its rectangle is derived by the host.
struct SetDeviceProtection { DeviceRef Device; bool Enabled; };
// The host chooses capabilities, credits, lifetime and identity. A proposal
// selects only an already approved program and a bounded execution rectangle.
struct InstallIntervention {
    ProgramRef Program;
    Cell Origin;
    Rect Scope;
    uint64_t ExpectedResourceVersion = 0;
};
// The current device determines the anchor and bounded region on the host.
struct InterveneAtDevice {
    ProgramRef Program;
    DeviceRef Device;
    uint64_t ExpectedResourceVersion = 0;
};
using Action = std::variant<MineRegion, PlaceMaterial, ActivateProgram, SelectWindStrategy, SetWindBoundary,
                            PlaceDevice, RemoveDevice, SupplyDevice, SetDeviceEnabled, ObserveForce, SetDeviceProtection,
                            InstallIntervention, InterveneAtDevice>;
struct ActionRequest {
    uint64_t WorldEpoch = 0, Sequence = 0;
    Action Payload;
    uint32_t SchemaVersion = 1;
};
enum class ActionStatus { Applied, Rejected };
enum class Reason {
    None, InvalidArgument, Unsupported, StaleEpoch, CallerExpired, DuplicateExpired,
    SessionUnavailable, WaitingForBudget, PermissionDenied, NotReady, Blocked,
    PreconditionFailed, StaleProgram, LaunchBlocked, LimitExceeded, StorageFailure, InvalidProgram,
    InventoryFull, InsufficientItems
};
const char *ReasonName(Reason reason);
struct MaterialResult {
    Cell Target;
    std::optional<uint32_t> Before, After;
    uint32_t Changed = 0, Blocked = 0, NotReady = 0;
    std::optional<items::ItemAmount> Collected, Consumed;
};
struct ActivationResult {
    uint64_t WorldEpoch = 0, ActivationId = 0;
    ProgramRef Program;
    uint64_t RunningRoots = 0, Projectiles = 0, Pending = 0;
};
struct WindResult { WindKind Kind; int64_t BoundaryY; };
using ActionResult = std::variant<std::monostate, MaterialResult, ActivationResult, WindResult, DeviceChanged, ForceObserved, AuthorityChanged>;
// Synchronous terminal result at a complete committed state.
struct ActionReceipt {
    RequestKey Key;
    ActionStatus Status = ActionStatus::Rejected;
    Reason Rejection = Reason::None;
    ActionResult Result;
    CommitStamp ResolvedAt;
    FactRange Facts;
    std::string Message;
    bool Applied() const { return Status == ActionStatus::Applied; }
};

struct SetPaused { bool Paused; };
struct StepSimulation {};
struct SaveWorld {};
struct ResetWorld {};
struct RetryStreaming {};
struct RecoverWorld {};
struct SetNextGenerator { GeneratorKind Kind; };
struct SetNextSeed { uint64_t Seed; };
struct ClearRunning {};
// Saves the current world, then creates a separate playable scene.
struct CreateForceGarden {};
using SessionControl = std::variant<SetPaused, StepSimulation, SaveWorld, ResetWorld, RetryStreaming,
                                    RecoverWorld, SetNextGenerator, SetNextSeed, ClearRunning, CreateForceGarden>;
enum class ControlStatus { Applied, Saved, Replaced, Deferred, Failed, Rejected };
struct ControlReceipt {
    ControlStatus Status = ControlStatus::Rejected;
    Reason Rejection = Reason::None;
    std::string Message;
    bool Succeeded() const { return Status == ControlStatus::Applied || Status == ControlStatus::Saved || Status == ControlStatus::Replaced; }
};
} // namespace pixel::api
