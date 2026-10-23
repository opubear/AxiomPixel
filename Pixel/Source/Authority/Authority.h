#pragma once
#include "GameAPI/Facts.h"
#include <span>
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
// One world-will subject and player-owned protection modules. These are game
// rules, not caller-provided roles or a general authorization registry.
class Control {
  public:
    void Reset(api::CallerSessionId subject);
    uint64_t Version() const { return Revision; }
    bool CanObserve(api::CallerSessionId subject, api::Rect scope) const;
    bool CanIntervene(api::CallerSessionId subject, api::Rect scope) const;
    bool SetCapabilities(Capabilities capabilities);
    api::AuthorityChanged SetProtection(const DeviceSnapshot &device, bool installed);
    std::vector<api::AuthorityChanged> Synchronize(std::span<const DeviceSnapshot> devices);
    bool DefaultCapabilities() const { return Grants == Capabilities{}; }
    bool HasProtection() const { return !Protections.empty(); }
    const std::vector<Protection> &Regions() const { return Protections; }
    static bool ValidRegion(api::Rect region);
    static bool Contains(api::Rect region, api::Cell point);

  private:
    api::CallerSessionId Subject = 0;
    uint64_t Revision = 1;
    Capabilities Grants;
    std::vector<Protection> Protections;
    bool Controlled(api::Rect scope) const;
    void Changed();
};
} // namespace pixel::authority
