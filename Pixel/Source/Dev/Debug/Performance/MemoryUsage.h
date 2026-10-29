#pragma once
#include "Core/Diagnostics/MemoryUsage.h"
#include <array>

namespace pixel::performance {
using axiom::diagnostics::MemoryUsage;

template<class Buffer>
MemoryUsage VectorStorage(const Buffer &values) {
    if constexpr (requires { values.UsedBytes(); values.ReservedBytes(); })
        return {0, values.UsedBytes(), values.ReservedBytes()};
    else
        return axiom::diagnostics::VectorStorage(values);
}

template<class Object, class... Buffers>
MemoryUsage MeasureObject(const Object &, const Buffers &...buffers) {
    MemoryUsage result{sizeof(Object)};
    (result.AddEmbedded(pixel::performance::VectorStorage(buffers)), ...);
    return result;
}

enum class MemoryComponent : size_t { World,
                                      Fields,
                                      BaseForces,
                                      Wind,
                                      Total,
                                      Count };
constexpr size_t MemoryComponentCount = size_t(MemoryComponent::Count);

inline std::string_view Name(MemoryComponent component) {
    constexpr auto names = std::to_array<std::string_view>({"memory.world", "memory.fields", "memory.base_forces", "memory.wind", "memory.total"});
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
        case MemoryComponent::World:
            return World;
        case MemoryComponent::Fields:
            return BaseForces + Wind;
        case MemoryComponent::BaseForces:
            return BaseForces;
        case MemoryComponent::Wind:
            return Wind;
        case MemoryComponent::Total:
            return World + BaseForces + Wind;
        default:
            return {};
        }
    }
};
} // namespace pixel::performance
