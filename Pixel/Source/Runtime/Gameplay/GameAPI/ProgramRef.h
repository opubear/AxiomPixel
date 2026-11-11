#pragma once
#include <cstdint>

namespace pixel::api {
struct ProgramRef {
    uint64_t ResourceId = 1, Version = 0;
    bool operator==(const ProgramRef &) const = default;
};
} // namespace pixel::api
