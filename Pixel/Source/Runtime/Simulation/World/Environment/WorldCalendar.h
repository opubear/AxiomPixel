#pragma once
#include <cstdint>

namespace pixel {
enum class Season : uint32_t { Spring, Summer, Autumn, Winter };
enum class DayPhase { Night, Dawn, Day, Dusk };
const char *SeasonName(Season season);
const char *DayPhaseName(DayPhase phase);

// Calendar ticks are 1/60 simulation second at normal speed. Integer arithmetic
// keeps midnight and season boundaries independent of render rate and rounding.
struct WorldCalendarState {
    uint64_t Day = 0; // zero based, from the first day of spring
    uint32_t TickOfDay = 24000; // 08:00 with the default twenty minute day
    uint32_t SecondsPerDay = 1200;
    uint32_t DaysPerSeason = 7;
    uint32_t TimeScale = 1; // calendar only; never accelerates physics
    bool operator==(const WorldCalendarState &) const = default;
};

struct WorldCalendarSnapshot {
    uint64_t Year = 1;
    uint32_t DayOfSeason = 1; // one based for display
    uint32_t Hour = 8, Minute = 0;
    Season CurrentSeason = Season::Spring;
    DayPhase Phase = DayPhase::Day;
    double DayProgress = 1.0 / 3;
    double SeasonProgress = 0;
    float SeasonBlend = 0; // towards the next season during the last quarter
    float SunriseHour = 6, SunsetHour = 18;
    float Daylight = 1; // smooth twilight, [0,1]
    float Twilight = 0;
};

class WorldCalendar {
  public:
    static constexpr uint32_t TicksPerSecond = 60;
    static void Validate(const WorldCalendarState &state);
    const WorldCalendarState &State() const { return Value; }
    void Restore(const WorldCalendarState &state);
    // Called once per successful fixed step. Large advances remain O(1).
    void Advance(uint64_t steps = 1);
    WorldCalendarSnapshot Snapshot() const;
    // Same-day target: nearest representable sunrise/sunset, noon for Day,
    // midnight for Night. Resolves season blending at the target time.
    uint32_t TickAtPhase(DayPhase phase) const;

  private:
    WorldCalendarState Value;
};
} // namespace pixel
