#pragma once
#include "GameAPI/Facts.h"
#include <string>
#include <vector>

namespace pixel::knowledge {
inline constexpr uint32_t SchemaVersion = 1;
inline constexpr size_t HistoryCapacity = 128, DiscoveryCapacity = 128;

// Identity is (WorldId, Sequence), independent of runtime epochs and ring slots.
struct HistoryRecord {
    uint64_t Sequence = 0;
    api::DomainFact Source;
};
struct Discovery {
    uint64_t OwnerActor = 0;
    api::Cell Location;
    uint64_t EvidenceSequence = 0;
    // These are independent knowledge permissions, never device/region control.
    bool Observed = true, Analyzed = false, CanCopy = false, CanEdit = false;
};
struct Snapshot {
    api::CommitStamp Stamp;
    uint64_t FirstSequence = 1, HighWatermark = 0;
    std::vector<HistoryRecord> History;
    std::vector<Discovery> Discoveries;
    uint64_t RetainedBytes() const;
};
enum class ReadStatus { Available, Gap, InvalidRange, LimitExceeded };
struct HistoryRead {
    ReadStatus Status = ReadStatus::Available;
    uint64_t FirstSequence = 1, HighWatermark = 0, NextSequence = 0;
    bool HasMore = false;
    std::vector<HistoryRecord> Records;
};
HistoryRead ReadHistory(const Snapshot &snapshot, uint64_t after, uint32_t limit);

// Owning, versioned checkpoint contract, encoded by Persistence/KnowledgeCodec.
struct SaveData {
    uint32_t Version = SchemaVersion;
    std::string WorldId;
    uint64_t NextSequence = 1;
    // Distinguishes this runtime's suffix even when a new process reuses an epoch.
    uint64_t RuntimeFirstSequence = 1;
    api::FactId LastSource;
    uint64_t LastRevision = 0, LastTick = 0;
    std::vector<HistoryRecord> History;
    std::vector<Discovery> Discoveries;
};
} // namespace pixel::knowledge
