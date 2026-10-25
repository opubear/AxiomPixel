#pragma once
#include <array>
#include <cstdint>

namespace pixel {
class World;
class CommittedWorldView;
}
class WorldGridFrame;

enum class WindVisualMode : uint32_t { Off,
                                       ShortTrails,
                                       AirRefraction };
struct WindVisualSettings {
    WindVisualMode Mode = WindVisualMode::AirRefraction;
    float Opacity = 0.3f;
    float RefractionPixels = 3.f;
    float MotionScale = 0.2f;
};
// One texel per world cell. Two signed 16-bit velocities at 1/32 cells/s.
// The unused minimum pair marks solid cells. Clamping is visual only.
inline constexpr uint32_t WindSolidTexel = 0x80008000u;
inline constexpr float WindVelocityScale = 32.f;
inline constexpr float WindVelocityLimit = 32767.f / WindVelocityScale;
uint32_t PackWindVelocity(float x, float y);
std::array<float, 2> UnpackWindVelocity(uint32_t packed);
void BuildWindVisualFrame(const pixel::World &world, const WindVisualSettings &settings,
                          float timeSeconds, WorldGridFrame &output);
void BuildWindVisualFrame(const pixel::CommittedWorldView &world, const WindVisualSettings &settings,
                          float timeSeconds, WorldGridFrame &output);
