#pragma once
#include "GameAPI/Facts.h"
#include <vector>

namespace pixel::authority {
struct Capabilities {
    bool Observe = true, Intervene = true;
    bool operator==(const Capabilities &) const = default;
};
struct Protection {
    uint64_t DeviceId = 0;
    api::Rect Region;
    bool Active = false;
};
struct Intervention {
    uint64_t Id = 0, ExpiresAtTick = 0;
    api::Rect Scope;
};
struct SaveData {
    uint64_t Version = 1;
    Capabilities Grants;
    api::InterventionResources Influence;
    std::vector<Protection> Protections;
    uint64_t NextIntervention = 1;
    std::vector<Intervention> Interventions;
};
} // namespace pixel::authority
