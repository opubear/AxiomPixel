#include "CalendarLighting.h"
#include <cmath>

namespace pixel {
PostProcessParameters CalendarLighting(const WorldCalendarSnapshot &calendar, PostProcessParameters base) {
    constexpr std::array<std::array<float, 3>, 4> seasons{{
        {.94f, 1.0f, .91f}, {1.0f, .96f, .84f}, {1.0f, .79f, .57f}, {.80f, .90f, 1.0f}}};
    constexpr std::array<float, 3> night{.15f, .21f, .38f}, twilight{1.0f, .65f, .44f};
    const auto season = size_t(calendar.CurrentSeason);
    for (size_t channel = 0; channel < 3; ++channel) {
        const auto daylight = std::lerp(seasons.at(season)[channel], seasons[(season + 1) % 4][channel], calendar.SeasonBlend);
        const auto sun = std::lerp(daylight, twilight[channel], calendar.Twilight * .65f);
        base.Values[channel] *= std::lerp(night[channel], sun, calendar.Daylight);
    }
    return base;
}
} // namespace pixel
