#pragma once
#include "Formula/Process/HostContract.h"

namespace pixel::formula::process {
struct Cells { double Value = 0; };
struct Speed { double Value = 0; };
struct Duration { std::uint64_t Value = 0; };
struct Acceleration { double Value = 0; };
using Value = std::variant<bool, std::int64_t, double, Cells, Speed, Duration, Effect, Outcome,
                           Acceleration, EffectHandle>;
} // namespace pixel::formula::process
