#include "DomainFactBuffer.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace pixel {
static_assert(sizeof(DomainFactBuffer) < DomainFactBuffer::MaximumRetainedBytes);

void DomainFactBuffer::Reset(uint64_t epoch) {
    if (epoch == 0)
        throw std::logic_error("fact buffer requires a nonzero world epoch");
    for (auto &fact : Facts)
        fact.reset();
    Epoch = epoch;
    NextSequence = 1;
    LastRevision = 0;
    Head = Count = StringBytes = 0;
}

void DomainFactBuffer::RemoveOldest() {
    StringBytes -= Facts[Head]->CommittedAt.WorldId.capacity() + 1;
    Facts[Head].reset();
    Head = (Head + 1) % Capacity;
    --Count;
}

api::FactId DomainFactBuffer::Append(const api::CommitStamp &stamp, api::FactCause cause, api::FactPayload payload) {
    if (Epoch == 0 || stamp.WorldEpoch != Epoch || stamp.Revision == 0 || stamp.Revision < LastRevision)
        throw std::logic_error("fact must belong to the current committed epoch and revision");
    if (NextSequence == UINT64_MAX)
        throw std::overflow_error("domain fact identity exhausted");
    const api::FactId id{Epoch, NextSequence++};
    LastRevision = stamp.Revision;
    api::DomainFact fact{id, stamp, std::move(cause), std::move(payload)};
    // Payloads and causes contain only fixed-size values. Count string capacity,
    // including the terminator; counting SSO capacity is conservative.
    const auto bytes = fact.CommittedAt.WorldId.capacity() + 1;
    while (Count != 0 && (Count == Capacity || bytes > MaximumRetainedBytes - RetainedBytes()))
        RemoveOldest();
    // Even an unretainable fact consumes its identity. The missing interval is
    // observable as Gap, including when this empties the entire retained window.
    if (bytes > MaximumRetainedBytes - RetainedBytes())
        return id;
    Facts[(Head + Count) % Capacity] = std::move(fact);
    StringBytes += bytes;
    ++Count;
    return id;
}

uint64_t DomainFactBuffer::FirstAvailableSequence() const {
    return Count == 0 ? NextSequence : Facts[Head]->Id.Sequence;
}

size_t DomainFactBuffer::RetainedBytes() const { return sizeof(*this) + StringBytes; }

api::FactReadResult DomainFactBuffer::Read(uint64_t epoch, uint64_t afterSequence, uint64_t highWatermark, size_t limit) const {
    api::FactReadResult result;
    result.WorldEpoch = Epoch;
    result.HighWatermark = highWatermark;
    result.FirstAvailableSequence = FirstAvailableSequence();
    result.NextSequence = afterSequence;
    if (epoch != Epoch || Epoch == 0) {
        result.Status = api::FactReadStatus::StaleEpoch;
        return result;
    }
    if (afterSequence > highWatermark || highWatermark > HighWatermark() || limit == 0) {
        result.Status = api::FactReadStatus::InvalidRange;
        return result;
    }
    if (limit > Capacity) {
        result.Status = api::FactReadStatus::LimitExceeded;
        return result;
    }
    if (afterSequence == highWatermark)
        return result;
    if (afterSequence + 1 < result.FirstAvailableSequence) {
        result.Status = api::FactReadStatus::Gap;
        return result;
    }

    const auto available = highWatermark - afterSequence;
    const auto returned = std::min<uint64_t>(available, limit);
    result.Facts.reserve(static_cast<size_t>(returned));
    const auto offset = static_cast<size_t>(afterSequence + 1 - result.FirstAvailableSequence);
    for (size_t i = 0; i < returned; ++i)
        result.Facts.push_back(*Facts[(Head + offset + i) % Capacity]);
    result.NextSequence = afterSequence + returned;
    result.HasMore = result.NextSequence < highWatermark;
    return result;
}
} // namespace pixel
