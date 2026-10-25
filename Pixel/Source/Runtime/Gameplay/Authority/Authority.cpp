#include "Authority.h"
#include <algorithm>
#include <stdexcept>
#include <set>

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
SaveData Control::CaptureState() const {
    SaveData data;
    data.Version = Revision;
    data.Grants = Grants;
    data.Influence = Influence;
    data.Protections = Protections;
    return data;
}

void Control::RestoreState(const SaveData &data, api::CallerSessionId subject, std::span<const DeviceSnapshot> devices) {
    if (!subject || !data.Version || data.Version == UINT64_MAX || !data.Influence.Version
        || data.Influence.Version == UINT64_MAX || data.Influence.Available > MaximumInterventions
        || data.Interventions.size() != MaximumInterventions - data.Influence.Available
        || data.Protections.size() > 64)
        throw std::invalid_argument("invalid authority checkpoint ledger");
    std::set<uint64_t> seen;
    for (const auto &protection : data.Protections) {
        const auto found = std::find_if(devices.begin(), devices.end(),
            [&](const auto &device) { return device.Ref.Id == protection.DeviceId; });
        if (found == devices.end() || !seen.insert(protection.DeviceId).second
            || !ValidRegion(protection.Region)
            || protection.Region != api::Rect{{found->X - 16, found->Y - 16}, DeviceWidth + 32, DeviceHeight + 32}
            || protection.Active != found->Enabled)
            throw std::invalid_argument("authority protection and device disagree");
    }
    Subject = subject;
    Revision = data.Version;
    Influence = data.Influence;
    Grants = data.Grants;
    Protections = data.Protections;
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
