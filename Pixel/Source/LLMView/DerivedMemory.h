#pragma once
#include "GameAPI/Facts.h"
#include <span>
#include <string_view>

namespace pixel::llm {
struct Response;

struct HistoryId {
    std::string WorldId;
    uint64_t Sequence = 0;
    bool operator==(const HistoryId &) const = default;
};
struct HistoryReference {
    HistoryId History;
    api::FactId SourceFact;
    api::CommitStamp SourceVersion;
    uint64_t RetainedBytes() const;
};
struct DerivedMemory {
    static constexpr std::string_view Provenance = "model_derived";
    uint64_t Id = 0;
    std::string Text;
    std::vector<HistoryReference> Sources;
    uint64_t RetainedBytes() const;
};
enum class MemoryStatus { Stored, MissingSources, InvalidSource, InvalidText, LimitExceeded };
struct MemoryResult {
    MemoryStatus Status = MemoryStatus::Stored;
    uint64_t Id = 0;
};

// Model text is non-authoritative data. This store has no domain, action or
// Journal handle. A delivery receipt proves only provenance, never correctness
// of the model's interpretation and never a new knowledge/control permission.
class DerivedMemoryStore {
  public:
    static constexpr size_t MaximumEntries = 16, MaximumTextBytes = 1024, MaximumSources = 8;
    static constexpr uint64_t MaximumRetainedBytes = 128 * 1024;
    MemoryResult Remember(const Response &delivered, std::string_view text, std::span<const HistoryId> sources);
    std::span<const DerivedMemory> Entries() const { return Values; }
    uint64_t RetainedBytes() const;

  private:
    std::vector<DerivedMemory> Values;
    uint64_t NextId = 1;
};
} // namespace pixel::llm
