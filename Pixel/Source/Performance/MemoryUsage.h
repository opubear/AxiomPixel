#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <vector>

namespace pixel::performance {
// Owned CPU storage, not allocator usable size or process RSS. ObjectBytes
// includes inline members/padding; vector metadata is counted there only once.
struct MemoryUsage {
    uint64_t ObjectBytes = 0;
    uint64_t UsedHeapBytes = 0;
    uint64_t ReservedHeapBytes = 0;

    uint64_t UsedBytes() const { return ObjectBytes + UsedHeapBytes; }

    uint64_t ReservedBytes() const { return ObjectBytes + ReservedHeapBytes; }

    bool operator==(const MemoryUsage &) const = default;

    void AddEmbedded(const MemoryUsage &member) {
        UsedHeapBytes += member.UsedHeapBytes;
        ReservedHeapBytes += member.ReservedHeapBytes;
    }

    void AddOwned(const MemoryUsage &pointee) {
        UsedHeapBytes += pointee.UsedBytes();
        ReservedHeapBytes += pointee.ReservedBytes();
    }

    // Partition an already-counted embedded component into a separate row.
    MemoryUsage ExcludingEmbedded(const MemoryUsage &member) const {
        assert(ObjectBytes >= member.ObjectBytes && UsedHeapBytes >= member.UsedHeapBytes &&
               ReservedHeapBytes >= member.ReservedHeapBytes);
        return {ObjectBytes - member.ObjectBytes, UsedHeapBytes - member.UsedHeapBytes,
                ReservedHeapBytes - member.ReservedHeapBytes};
    }
};

inline MemoryUsage operator+(const MemoryUsage &a, const MemoryUsage &b) {
    return {a.ObjectBytes + b.ObjectBytes, a.UsedHeapBytes + b.UsedHeapBytes,
            a.ReservedHeapBytes + b.ReservedHeapBytes};
}

// Only flat, trivially-copyable elements: nested ownership must be accounted
// explicitly by its owner, rather than silently counting vector/string handles.
template <class T>
MemoryUsage VectorStorage(const std::vector<T> &values) {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_same_v<T, bool>);
    return {0, uint64_t(values.size()) * sizeof(T), uint64_t(values.capacity()) * sizeof(T)};
}

template <class Object, class... Elements>
MemoryUsage MeasureObject(const Object &, const std::vector<Elements> &...buffers) {
    MemoryUsage result{sizeof(Object)};
    (result.AddEmbedded(VectorStorage(buffers)), ...);
    return result;
}

enum class MemoryComponent : size_t { World, Fields, BaseForces, Wind, Total, Count };
constexpr size_t MemoryComponentCount = size_t(MemoryComponent::Count);

inline std::string_view Name(MemoryComponent component) {
    constexpr auto names = std::to_array<std::string_view>({
        "memory.world", "memory.fields", "memory.base_forces", "memory.wind", "memory.total"});
    static_assert(names.size() == MemoryComponentCount);
    return names.at(size_t(component));
}

// Three disjoint owners. Aggregate rows are derived from the same snapshot,
// so a peak total cannot accidentally sum peaks from different frames.
struct MemorySnapshot {
    MemoryUsage World;
    MemoryUsage BaseForces;
    MemoryUsage Wind;

    MemoryUsage Get(MemoryComponent component) const {
        switch (component) {
        case MemoryComponent::World: return World;
        case MemoryComponent::Fields: return BaseForces + Wind;
        case MemoryComponent::BaseForces: return BaseForces;
        case MemoryComponent::Wind: return Wind;
        case MemoryComponent::Total: return World + BaseForces + Wind;
        default: return {};
        }
    }
};
} // namespace pixel::performance
