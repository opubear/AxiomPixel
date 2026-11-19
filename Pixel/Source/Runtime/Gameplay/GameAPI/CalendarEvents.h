#pragma once
#include "Core/Events/EventDispatcher.h"
#include "Runtime/Gameplay/GameAPI/CommitStamp.h"
#include "Runtime/Simulation/World/Environment/WorldCalendar.h"

namespace pixel::api {
struct AXIOM_PUBLIC_TYPE CalendarTransition {
    WorldCalendarSnapshot Previous, Current;
    CommitStamp Commit;
};

struct CalendarEvents final : axiom::EventDispatcher<axiom::NameDomain::Simulation> {
    using ChangedSignature = void(const CalendarTransition &);
    const Name DayPhaseChanged = Declare<ChangedSignature>(Name{"Pixel.Calendar.DayPhaseChanged"});
    const Name SeasonChanged = Declare<ChangedSignature>(Name{"Pixel.Calendar.SeasonChanged"});

    void BroadcastChanges(const CalendarTransition &change) {
        if (change.Previous.CurrentSeason != change.Current.CurrentSeason)
            Broadcast<ChangedSignature>(SeasonChanged, change);
        if (change.Previous.Phase != change.Current.Phase)
            Broadcast<ChangedSignature>(DayPhaseChanged, change);
    }
};
} // namespace pixel::api
