#pragma once
#include "KnowledgeTypes.h"

namespace pixel::knowledge {
// Single writer. Only committed host facts enter; model text has no write port.
class Journal {
  public:
    void Reset();
    void Record(const api::DomainFact &fact);
    bool CanDiscover(uint64_t actor, api::Cell location) const;
    Snapshot Capture(const api::CommitStamp &stamp) const;
    SaveData Export() const { return Data; }
    void Restore(const SaveData &data);
    // Call on each loaded runtime, even if an old process reused the epoch number.
    void BeginRuntime(uint64_t epoch);
    uint64_t RetainedBytes() const;

  private:
    SaveData Data;
};
} // namespace pixel::knowledge
