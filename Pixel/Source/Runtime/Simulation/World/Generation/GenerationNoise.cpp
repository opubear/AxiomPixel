#include "GenerationNoise.h"
#include "Runtime/Simulation/World/WorldCoordinates.h"
#include <algorithm>
#include <bit>
#include <stdexcept>

namespace pixel::generation {
uint64_t StableMix(uint64_t value) {
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

uint64_t ChannelSeed(uint64_t seed, std::string_view tag) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : tag)
        hash = (hash ^ byte) * 1099511628211ull;
    return StableMix(seed ^ hash);
}

uint64_t FeatureHash(uint64_t channel, int64_t x, int64_t y) {
    return StableMix(channel ^ StableMix(uint64_t(x) + 0x9e3779b97f4a7c15ull) ^
                     std::rotl(StableMix(uint64_t(y) + 0xd1b54a32d192ed03ull), 32));
}

int32_t FixedMultiply(int32_t a, int32_t b) {
    return int32_t(FloorDivide(int64_t(a) * b, FixedOne));
}

int32_t FixedFade(int32_t t) {
    const int32_t t2 = FixedMultiply(t, t), t3 = FixedMultiply(t2, t);
    const int32_t t4 = FixedMultiply(t3, t), t5 = FixedMultiply(t4, t);
    return std::clamp(6 * t5 - 15 * t4 + 10 * t3, 0, FixedOne);
}

int64_t FixedRound(int64_t value) {
    return FloorDivide(value + FixedOne / 2, FixedOne);
}

namespace {
int32_t Gradient(uint64_t channel, int64_t x, int64_t y, int32_t dx, int32_t dy) {
    // Axis and diagonal gradients have integer components -1/0/1. The v1
    // normalization coefficient is exactly 1; the final output is clipped.
    switch (FeatureHash(channel, x, y) & 7) {
    case 0: return dx;
    case 1: return -dx;
    case 2: return dy;
    case 3: return -dy;
    case 4: return dx + dy;
    case 5: return dx - dy;
    case 6: return -dx + dy;
    default: return -dx - dy;
    }
}

int32_t Interpolate(int32_t a, int32_t b, int32_t t) {
    return a + FixedMultiply(b - a, t);
}
} // namespace

int32_t Perlin(uint64_t channel, int64_t x, int64_t y, uint32_t scaleX, uint32_t scaleY) {
    if (!std::has_single_bit(scaleX) || !std::has_single_bit(scaleY) ||
        scaleX < 8 || scaleY < 8 || scaleX > 16384 || scaleY > 16384)
        throw std::invalid_argument("noise scale must be a power of two in [8,16384]");

    const int64_t latticeX = FloorDivide(x, scaleX), latticeY = FloorDivide(y, scaleY);
    const int32_t tx = int32_t(((x - latticeX * scaleX) * 2 + 1) * (FixedOne / 2) / scaleX);
    const int32_t ty = int32_t(((y - latticeY * scaleY) * 2 + 1) * (FixedOne / 2) / scaleY);
    const int32_t fadeX = FixedFade(tx), fadeY = FixedFade(ty);
    const int32_t top = Interpolate(Gradient(channel, latticeX, latticeY, tx, ty),
                                    Gradient(channel, latticeX + 1, latticeY, tx - FixedOne, ty), fadeX);
    const int32_t bottom = Interpolate(Gradient(channel, latticeX, latticeY + 1, tx, ty - FixedOne),
                                       Gradient(channel, latticeX + 1, latticeY + 1, tx - FixedOne, ty - FixedOne), fadeX);
    return std::clamp(Interpolate(top, bottom, fadeY), -FixedOne, FixedOne);
}
} // namespace pixel::generation
