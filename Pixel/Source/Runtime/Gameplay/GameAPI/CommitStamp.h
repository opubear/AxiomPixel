#pragma once
#include <cstdint>
#include <string>

namespace pixel::api {
struct CommitStamp {
    std::string WorldId;
    uint64_t WorldEpoch = 0, Tick = 0, Revision = 0;
    bool operator==(const CommitStamp &) const = default;
};
} // namespace pixel::api
