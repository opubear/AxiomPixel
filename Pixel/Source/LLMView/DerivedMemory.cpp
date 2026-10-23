#include "DerivedMemory.h"
#include "ViewService.h"
#include <algorithm>
#include <stdexcept>

namespace pixel::llm {
uint64_t HistoryReference::RetainedBytes() const {
    return sizeof(*this) + History.WorldId.capacity() + 1 + SourceVersion.WorldId.capacity() + 1;
}

uint64_t DerivedMemory::RetainedBytes() const {
    uint64_t bytes = sizeof(*this) + Text.capacity() + 1 + Sources.capacity() * sizeof(HistoryReference);
    for (const auto &source : Sources) bytes += source.RetainedBytes() - sizeof(source);
    return bytes;
}

uint64_t DerivedMemoryStore::RetainedBytes() const {
    uint64_t bytes = sizeof(*this) + Values.capacity() * sizeof(DerivedMemory);
    for (const auto &value : Values) bytes += value.RetainedBytes() - sizeof(value);
    return bytes;
}

MemoryResult DerivedMemoryStore::Remember(const Response &delivered, std::string_view text,
                                         std::span<const HistoryId> sources) {
    if (sources.empty()) return {MemoryStatus::MissingSources};
    if (text.empty()) return {MemoryStatus::InvalidText};
    if (text.size() > MaximumTextBytes || sources.size() > MaximumSources || Values.size() == MaximumEntries)
        return {MemoryStatus::LimitExceeded};
    if (delivered.Result != Status::Ok || !delivered.DeliveredHistory) return {MemoryStatus::InvalidSource};

    DerivedMemory memory;
    memory.Id = NextId;
    memory.Text = text;
    memory.Sources.reserve(sources.size());
    for (const auto &requested : sources) {
        const auto &evidence = *delivered.DeliveredHistory;
        const auto found = std::find_if(evidence.begin(), evidence.end(), [&](const auto &source) {
            return source.History == requested;
        });
        if (found == evidence.end()) return {MemoryStatus::InvalidSource};
        if (std::any_of(memory.Sources.begin(), memory.Sources.end(), [&](const auto &source) {
            return source.History == requested;
        })) return {MemoryStatus::InvalidSource};
        memory.Sources.push_back(*found);
    }
    // Reserve once so the storage budget includes future entry slots too.
    const uint64_t retained = RetainedBytes() + (MaximumEntries - Values.capacity()) * sizeof(DerivedMemory)
        + memory.RetainedBytes() - sizeof(memory);
    if (retained > MaximumRetainedBytes) return {MemoryStatus::LimitExceeded};
    if (NextId == UINT64_MAX) throw std::overflow_error("derived memory identity exhausted");
    Values.reserve(MaximumEntries);
    Values.push_back(std::move(memory));
    return {MemoryStatus::Stored, NextId++};
}
} // namespace pixel::llm
