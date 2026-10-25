#pragma once

#include "Runtime/Gameplay/GameAPI/Facts.h"
#include <array>
#include <cstddef>
#include <optional>

namespace pixel {
// Single-writer storage. The host appends an entire committed batch before
// exposing its new high watermark to observers.
class DomainFactBuffer {
  public:
    static constexpr size_t Capacity = 256;
    static constexpr size_t MaximumRetainedBytes = 128 * 1024;

    void Reset(uint64_t epoch);
    api::FactId Append(const api::CommitStamp &stamp, api::FactCause cause, api::FactPayload payload);
    api::FactReadResult Read(uint64_t epoch, uint64_t afterSequence, uint64_t highWatermark, size_t limit = Capacity) const;
    uint64_t HighWatermark() const { return NextSequence - 1; }
    uint64_t FirstAvailableSequence() const;
    size_t RetainedBytes() const;
    size_t Size() const { return Count; }

  private:
    std::array<std::optional<api::DomainFact>, Capacity> Facts;
    uint64_t Epoch = 0, NextSequence = 1, LastRevision = 0;
    size_t Head = 0, Count = 0, StringBytes = 0;

    void RemoveOldest();
};
} // namespace pixel
