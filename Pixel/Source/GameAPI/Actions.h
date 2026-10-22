#pragma once
#include "CommitStamp.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace pixel::api {
using CallerSessionId = uint64_t;
struct Cell {
    int64_t X = 0, Y = 0;
    bool operator==(const Cell &) const = default;
};
struct Position {
    Cell AnchorCell;
    std::array<double, 2> Offset{}; // Normalized to [0,1), in cells.
};
struct ProgramRef {
    uint64_t ResourceId = 1, Version = 0;
    bool operator==(const ProgramRef &) const = default;
};
enum class WindKind { Coarse, Reference, Procedural, Hybrid, Adaptive, Lbm, Empty };
enum class GeneratorKind { Flat, Natural };
struct MineRegion { Cell Target; std::optional<uint32_t> ExpectedMaterial; };
struct PlaceMaterial { Cell Target; uint32_t MaterialId = 10; };
struct ActivateProgram { ProgramRef Program; Position Target; };
struct SelectWindStrategy { WindKind Kind; };
struct SetWindBoundary { int64_t Y; };
using Action = std::variant<MineRegion, PlaceMaterial, ActivateProgram, SelectWindStrategy, SetWindBoundary>;
struct ActionRequest {
    uint64_t WorldEpoch = 0, Sequence = 0;
    Action Payload;
    uint32_t SchemaVersion = 1;
};
struct RequestKey {
    uint64_t WorldEpoch = 0;
    CallerSessionId Caller = 0;
    uint64_t Sequence = 0;
    bool operator==(const RequestKey &) const = default;
};
enum class ActionStatus { Applied, Rejected };
enum class Reason {
    None, InvalidArgument, Unsupported, StaleEpoch, CallerExpired, DuplicateExpired,
    SessionUnavailable, WaitingForBudget, PermissionDenied, NotReady, Blocked,
    PreconditionFailed, StaleProgram, LaunchBlocked, LimitExceeded, StorageFailure, InvalidProgram
};
const char *ReasonName(Reason reason);
struct MaterialResult {
    Cell Target;
    std::optional<uint32_t> Before, After;
    uint32_t Changed = 0, Blocked = 0, NotReady = 0;
};
struct ActivationResult {
    uint64_t WorldEpoch = 0, ActivationId = 0;
    ProgramRef Program;
    uint64_t RunningRoots = 0, Projectiles = 0, Pending = 0;
};
struct WindResult { WindKind Kind; int64_t BoundaryY; };
using ActionResult = std::variant<std::monostate, MaterialResult, ActivationResult, WindResult>;
// Synchronous terminal result at a complete committed state; facts follow in M2.4.
struct ActionReceipt {
    RequestKey Key;
    ActionStatus Status = ActionStatus::Rejected;
    Reason Rejection = Reason::None;
    ActionResult Result;
    CommitStamp ResolvedAt;
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
using SessionControl = std::variant<SetPaused, StepSimulation, SaveWorld, ResetWorld, RetryStreaming,
                                    RecoverWorld, SetNextGenerator, SetNextSeed, ClearRunning>;
enum class ControlStatus { Applied, Saved, Replaced, Failed, Rejected };
struct ControlReceipt {
    ControlStatus Status = ControlStatus::Rejected;
    Reason Rejection = Reason::None;
    std::string Message;
    bool Succeeded() const { return Status != ControlStatus::Rejected && Status != ControlStatus::Failed; }
};
} // namespace pixel::api
