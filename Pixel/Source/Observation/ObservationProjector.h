#pragma once
#include "GameAPI/Observations.h"
#include <memory>

namespace pixel {
class World;
class FormulaBridge;

// Single-writer service. Three native slots and one external capture slot are independent of rendering and
// never request residency. External observer authorization/leases live in LLMView.
class ObservationProjector {
  public:
    static constexpr size_t MaximumScopes = 4;
    static constexpr uint32_t MaximumEdge = 64;
    static constexpr uint64_t MaximumSnapshotBytes = 1024 * 1024;
    ObservationProjector();
    ~ObservationProjector();
    api::ObservationResult Capture(size_t slot, api::Rect scope, const World &world,
        const FormulaBridge &formulas, const api::CommitStamp &stamp, bool paused, bool ready,
        bool pending, uint64_t factHighWatermark);
    void Publish(const World &world, const FormulaBridge &formulas,
        const api::CommitStamp &stamp, bool paused, uint64_t factHighWatermark);
    void Close(size_t slot);
    void Reset();
    api::ObservationMetrics Metrics() const;

  private:
    struct Entry;
    std::array<std::unique_ptr<Entry>, MaximumScopes> Entries;
    api::ObservationMetrics Counters;
    void Update(Entry &entry, const World &world, const FormulaBridge &formulas,
        const api::CommitStamp &stamp, bool paused, uint64_t factHighWatermark);
};
} // namespace pixel
