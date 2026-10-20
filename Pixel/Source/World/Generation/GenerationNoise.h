#pragma once

#include <cstdint>
#include <string_view>

namespace pixel::generation {
inline constexpr int32_t FixedOne = 65536;

// Noise v1 uses modulo-2^64 SplitMix finalization and FNV-1a ASCII tags.
// Signed lattice coordinates are encoded by their uint64_t conversion.
uint64_t StableMix(uint64_t value);
uint64_t ChannelSeed(uint64_t seed, std::string_view tag);
uint64_t FeatureHash(uint64_t channel, int64_t x, int64_t y = 0);

// Scales are powers of two in [8,16384]. Samples are at cell centres.
// All interpolation is Q16.16, with products rounded toward negative infinity.
int32_t Perlin(uint64_t channel, int64_t x, int64_t y, uint32_t scaleX, uint32_t scaleY);
int32_t FixedMultiply(int32_t a, int32_t b);
int32_t FixedFade(int32_t t);
int64_t FixedRound(int64_t value);
} // namespace pixel::generation
