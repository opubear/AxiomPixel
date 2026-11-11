#include "WorldCalendar.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace pixel {
namespace {
float Smooth(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3 - 2 * value);
}
} // namespace

const char *SeasonName(Season season) {
    switch (season) {
    case Season::Spring: return "Spring";
    case Season::Summer: return "Summer";
    case Season::Autumn: return "Autumn";
    case Season::Winter: return "Winter";
    }
    throw std::invalid_argument("invalid season");
}

const char *DayPhaseName(DayPhase phase) {
    switch (phase) {
    case DayPhase::Night: return "Night";
    case DayPhase::Dawn: return "Dawn";
    case DayPhase::Day: return "Day";
    case DayPhase::Dusk: return "Dusk";
    }
    throw std::invalid_argument("invalid day phase");
}

void WorldCalendar::Validate(const WorldCalendarState &state) {
    if (!state.SecondsPerDay || state.SecondsPerDay > 86400
        || !state.DaysPerSeason || state.DaysPerSeason > 365
        || !state.TimeScale || state.TimeScale > 3600
        || state.TickOfDay >= state.SecondsPerDay * TicksPerSecond
        || state.Day == UINT64_MAX)
        throw std::invalid_argument("invalid world calendar state");
}

void WorldCalendar::Restore(const WorldCalendarState &state) {
    Validate(state);
    Value = state;
}

void WorldCalendar::Advance(uint64_t steps) {
    if (steps > (UINT64_MAX - Value.TickOfDay) / Value.TimeScale)
        throw std::overflow_error("world calendar advance exhausted");
    const auto ticks = Value.TickOfDay + steps * Value.TimeScale;
    const auto ticksPerDay = Value.SecondsPerDay * TicksPerSecond;
    const auto days = ticks / ticksPerDay;
    if (days >= UINT64_MAX - Value.Day)
        throw std::overflow_error("world calendar day exhausted");
    Value.Day += days;
    Value.TickOfDay = uint32_t(ticks % ticksPerDay);
}

WorldCalendarSnapshot WorldCalendar::Snapshot() const {
    WorldCalendarSnapshot result;
    const auto season = (Value.Day / Value.DaysPerSeason) % 4;
    const auto dayOfSeason = Value.Day % Value.DaysPerSeason;
    const auto ticksPerDay = Value.SecondsPerDay * TicksPerSecond;
    const auto minutes = uint64_t(Value.TickOfDay) * 1440 / ticksPerDay;
    result.Year = Value.Day / (4 * Value.DaysPerSeason) + 1;
    result.DayOfSeason = uint32_t(dayOfSeason + 1);
    result.CurrentSeason = Season(season);
    result.Hour = uint32_t(minutes / 60);
    result.Minute = uint32_t(minutes % 60);
    result.DayProgress = double(Value.TickOfDay) / ticksPerDay;
    result.SeasonProgress = (dayOfSeason + result.DayProgress) / Value.DaysPerSeason;
    result.SeasonBlend = Smooth(float((result.SeasonProgress - .75) * 4));

    constexpr std::array<float, 4> daylightHours{12, 14, 12, 10};
    const float length = std::lerp(daylightHours[season], daylightHours[(season + 1) % 4], result.SeasonBlend);
    result.SunriseHour = 12 - length / 2;
    result.SunsetHour = 12 + length / 2;
    const float hour = float(result.DayProgress * 24);
    result.Daylight = Smooth(hour - result.SunriseHour + .5f) * Smooth(result.SunsetHour + .5f - hour);
    const float horizonDistance = std::min(std::abs(hour - result.SunriseHour), std::abs(hour - result.SunsetHour));
    result.Twilight = Smooth(1 - horizonDistance / 1.5f);
    if (hour < result.SunriseHour - .5f || hour >= result.SunsetHour + .5f) result.Phase = DayPhase::Night;
    else if (hour < result.SunriseHour + .5f) result.Phase = DayPhase::Dawn;
    else if (hour >= result.SunsetHour - .5f) result.Phase = DayPhase::Dusk;
    else result.Phase = DayPhase::Day;
    return result;
}

uint32_t WorldCalendar::TickAtPhase(DayPhase phase) const {
    const auto ticksPerDay = Value.SecondsPerDay * TicksPerSecond;
    switch (phase) {
    case DayPhase::Night: return 0;
    case DayPhase::Day: return ticksPerDay / 2;
    case DayPhase::Dawn:
    case DayPhase::Dusk: break;
    default: throw std::invalid_argument("invalid day phase");
    }

    auto sample = *this;
    const auto distance = [&](uint32_t tick) {
        sample.Value.TickOfDay = tick;
        const auto snapshot = sample.Snapshot();
        const auto horizon = phase == DayPhase::Dawn ? snapshot.SunriseHour : snapshot.SunsetHour;
        return snapshot.DayProgress * 24 - horizon;
    };
    // The horizon moves at most 0.25 game hour per game hour (one-day seasons),
    // so time minus horizon increases strictly. Search integer ticks to bound
    // the work and choose the closest representable crossing, without iteration
    // drift or dependence on the current time of day.
    uint32_t lower = 0, upper = ticksPerDay - 1;
    while (lower < upper) {
        const auto middle = lower + (upper - lower) / 2;
        if (distance(middle) < 0) lower = middle + 1;
        else upper = middle;
    }
    if (lower > 0 && std::abs(distance(lower - 1)) < std::abs(distance(lower))) return lower - 1;
    return lower;
}
} // namespace pixel
