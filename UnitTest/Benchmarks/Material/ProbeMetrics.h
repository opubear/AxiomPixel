#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// Research-only counters and a deliberately immutable-field cache. The runner
// injects these into a COPY of Environment.cpp; production code is untouched.
namespace probe {
inline uint64_t Chains = 0, Samples = 0, Links = 0, Moves = 0;
inline size_t MaxChain = 0, Capacity = 0, Growths = 0;
inline std::vector<uint32_t> Destinations;
inline constexpr uint32_t NoDestination = std::numeric_limits<uint32_t>::max();
inline void ResetCounters() {
    Chains = Samples = Links = Moves = 0;
    MaxChain = Capacity = Growths = 0;
}
}
