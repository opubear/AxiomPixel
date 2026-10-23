#include "Authority.h"
#include <algorithm>
#include <stdexcept>

namespace pixel::authority {
namespace {
bool Overlaps(api::Rect a, api::Rect b) {
    return a.Origin.X < b.Origin.X + b.Width && b.Origin.X < a.Origin.X + a.Width
        && a.Origin.Y < b.Origin.Y + b.Height && b.Origin.Y < a.Origin.Y + a.Height;
}
}
bool Control::ValidRegion(api::Rect region) {
    // Same public range for every chunk configuration; leave room for offsets.
    constexpr int64_t limit = int64_t{1} << 54;
    return region.Width && region.Height && region.Width <= 64 && region.Height <= 64
        && region.Origin.X >= -limit && region.Origin.X <= limit - region.Width
        && region.Origin.Y >= -limit && region.Origin.Y <= limit - region.Height;
}
bool Control::Contains(api::Rect region, api::Cell point) {
    return ValidRegion(region) && point.X >= region.Origin.X && point.X < region.Origin.X + region.Width
        && point.Y >= region.Origin.Y && point.Y < region.Origin.Y + region.Height;
}
void Control::Changed() {
    if (Revision == UINT64_MAX) throw std::overflow_error("authority revision exhausted");
    ++Revision;
}
void Control::Reset(api::CallerSessionId subject) {
    if (!subject) throw std::logic_error("authority requires host identity");
    Subject = subject;
    Revision = 1;
    Influence = {};
    Grants = {};
    Protections.clear();
}
void Control::ReserveIntervention() {
    if (!Influence.Available) throw std::logic_error("intervention reserved without influence capacity");
    if (Influence.Version == UINT64_MAX) throw std::overflow_error("intervention resource version exhausted");
    --Influence.Available;
    ++Influence.Version;
}
void Control::ReleaseIntervention() {
    if (Influence.Available == MaximumInterventions) throw std::logic_error("intervention influence released twice");
    if (Influence.Version == UINT64_MAX) throw std::overflow_error("intervention resource version exhausted");
    ++Influence.Available;
    ++Influence.Version;
}
bool Control::Controlled(api::Rect scope) const {
    return ValidRegion(scope) && std::none_of(Protections.begin(), Protections.end(),
        [&](const auto &protection) { return protection.Active && Overlaps(protection.Region, scope); });
}
bool Control::CanObserve(api::CallerSessionId subject, api::Rect scope) const {
    return subject == Subject && subject && Grants.Observe && Controlled(scope);
}
bool Control::CanIntervene(api::CallerSessionId subject, api::Rect scope) const {
    return subject == Subject && subject && Grants.Intervene && Controlled(scope);
}
bool Control::SetCapabilities(Capabilities capabilities) {
    if (Grants == capabilities) return false;
    Grants = capabilities;
    Changed();
    return true;
}
api::AuthorityChanged Control::SetProtection(const DeviceSnapshot &device, bool installed) {
    auto found = std::find_if(Protections.begin(), Protections.end(),
        [&](const auto &entry) { return entry.DeviceId == device.Ref.Id; });
    const api::Rect region{{device.X - 16, device.Y - 16}, DeviceWidth + 32, DeviceHeight + 32};
    if (!ValidRegion(region)) throw std::logic_error("device protection outside supported coordinates");
    if (installed && found == Protections.end()) {
        if (Protections.size() == 64) throw std::logic_error("protection count exceeds device count");
        Protections.push_back({device.Ref.Id, region, device.Enabled});
        Changed();
    } else if (!installed && found != Protections.end()) {
        Protections.erase(found);
        Changed();
    }
    return {Revision, device.Ref.Id, region, installed && device.Enabled, Grants.Observe, Grants.Intervene};
}
std::vector<api::AuthorityChanged> Control::Synchronize(std::span<const DeviceSnapshot> devices) {
    std::vector<api::AuthorityChanged> changes;
    for (auto it = Protections.begin(); it != Protections.end();) {
        const auto device = std::find_if(devices.begin(), devices.end(),
            [&](const auto &entry) { return entry.Ref.Id == it->DeviceId; });
        if (device == devices.end() || device->Enabled != it->Active) {
            it->Active = device != devices.end() && device->Enabled;
            Changed();
            changes.push_back({Revision, it->DeviceId, it->Region, it->Active, Grants.Observe, Grants.Intervene});
        }
        if (device == devices.end()) it = Protections.erase(it);
        else ++it;
    }
    return changes;
}
} // namespace pixel::authority
