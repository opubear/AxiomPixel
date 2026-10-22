#pragma once
#include "Actions.h"
#include <string_view>
#include <vector>

namespace pixel::api {
struct Rect {
    Cell Origin;
    uint32_t Width = 0, Height = 0;
    bool operator==(const Rect &) const = default;
};

enum class ObservationStatus { Available, NotSampled, SessionUnavailable, InvalidArgument, LimitExceeded };
enum class Topic { Materials, AirVelocity, Player, Programs, Runtime, Temperature, Ecology, Items, Devices, Knowledge };
enum class Availability { Available, Partial, NotApplicable, Unknown, Unsupported };
constexpr Availability TopicSupport(Topic topic) {
    switch (topic) {
    case Topic::Materials: case Topic::AirVelocity: case Topic::Player: case Topic::Programs: case Topic::Runtime:
        return Availability::Available;
    default: return Availability::Unsupported;
    }
}

enum class AirSampleKind { NotResident, NotApplicable, Sampled, OutsideDomain, EmptyBackend, Disabled };
struct ObservedCell {
    std::optional<uint32_t> MaterialId; // No value means not resident, never Air.
    AirSampleKind AirKind = AirSampleKind::NotResident;
    std::optional<std::array<float, 2>> AirVelocity; // cell/second, at the cell centre.
    uint64_t MaterialSampledAtRevision = 0, WindSampledAtRevision = 0;
};

struct WindSampling {
    uint32_t ContractVersion = 1;
    WindKind Backend = WindKind::Empty;
    bool Enabled = false;
    Rect Domain;
    // Absent for Empty/procedural/adaptive (no single uniform solver grid).
    std::optional<std::array<double, 2>> SolverSpacingCells;
    std::optional<uint32_t> AdaptiveMaximumEdgeCells;
    bool LocalObstacleCorrection = false; // Hybrid/procedural cell-scale geometry.
};

struct PlayerObservation {
    Position Location; // cell; singleton player of this snapshot's epoch.
    std::array<float, 2> SizeCells{2, 4};
    bool Grounded = false;
};
struct ProjectileObservation {
    uint64_t InstanceId = 0, ProjectileId = 0; // Never array slots; epoch is in Stamp.
    ProgramRef Program;
    Position Location;
    std::array<double, 2> VelocityCellsPerSecond{};
    double RadiusCells = 0;
};
struct ProgramObservation {
    ProgramRef Applied;
    uint32_t RunningRoots = 0, Projectiles = 0, Pending = 0; // Session-wide counts.
    // The old prototype has no artifact/source-access contract; no source text.
};

struct ObservationSnapshot {
    static constexpr uint32_t SchemaVersion = 1, ResolutionCells = 1;
    static constexpr std::string_view MaterialSource = "world.material";
    static constexpr std::string_view WindSource = "environment.air_velocity.cell_center";
    static constexpr std::string_view ObjectSource = "session.player_and_formula_runtime";
    CommitStamp Stamp;
    Rect Scope;
    uint64_t FactHighWatermark = 0;
    bool Paused = true;
    WindSampling Wind;
    std::vector<ObservedCell> Cells; // Row-major over Scope, including missing cells.
    uint32_t ResidentCellCount = 0; // Coverage denominator is Scope.Width * Scope.Height.
    std::optional<PlayerObservation> Player; // Present if player rectangle intersects Scope.
    ProgramObservation Programs;
    std::vector<ProjectileObservation> Projectiles; // Centres within Scope.
    uint64_t ObjectSampledAtRevision = 0;
    uint64_t RetainedBytes() const;
    Availability MaterialAvailability() const {
        return ResidentCellCount == uint64_t(Scope.Width) * Scope.Height ? Availability::Available : Availability::Partial;
    }
};
struct ObservationResult {
    ObservationStatus Status = ObservationStatus::NotSampled;
    std::optional<ObservationSnapshot> Snapshot;
};
// Live service diagnostics are deliberately separate from a frozen domain DTO.
struct ObservationServiceStatus {
    CommitStamp LastCommitted;
    bool SessionReady = false, WaitingForBudget = false;
};

struct WindSummary {
    Rect Scope; // Intersection of the request with a world-aligned 16-cell bin.
    uint32_t RequestedCells = 0, ResidentCells = 0, AirSamples = 0, NonAirCells = 0, UnknownCells = 0;
    uint32_t OutsideDomainSamples = 0, EmptyBackendSamples = 0, DisabledSamples = 0;
    uint64_t OldestSampledAtRevision = 0, NewestSampledAtRevision = 0;
    Availability Status = Availability::Partial;
    std::optional<std::array<double, 2>> MeanVelocityCellsPerSecond;
    double CoverageFraction() const { return RequestedCells ? double(ResidentCells) / RequestedCells : 0; }
};
// Pure aggregation over an owned DTO. Missing/non-air cells never enter the mean.
std::vector<WindSummary> SummarizeWind(const ObservationSnapshot &snapshot);

struct ObservationMetrics {
    uint64_t RetainedBytes = 0, MaximumRetainedBytes = 0;
    uint64_t MaterialCellsSampled = 0, WindCellsSampled = 0;
    uint64_t CellBytesWritten = 0, ExportedBytes = 0;
    uint32_t ActiveScopes = 0;
};
} // namespace pixel::api
