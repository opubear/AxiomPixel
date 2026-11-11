#pragma once
#include "WorldGridFrame.h"
#include "Runtime/Simulation/World/Environment/WorldCalendar.h"

namespace pixel {
// Apply only to the material scene. UI and diagnostic field colours stay intact.
PostProcessParameters CalendarLighting(const WorldCalendarSnapshot &calendar, PostProcessParameters base = {});
} // namespace pixel
