#include "Devices.h"
#include <stdexcept>

namespace pixel {
const char *DeviceStateName(DeviceState state) {
    switch (state) {
    case DeviceState::Stopped: return "Stopped";
    case DeviceState::Running: return "Running";
    case DeviceState::NeedsFuel: return "Needs fuel (coal)";
    case DeviceState::WaitingForEnvironment: return "Needs clear air";
    case DeviceState::OutsideActiveArea: return "Outside active area";
    case DeviceState::WaitingForCapacity: return "Waiting for program capacity";
    }
    throw std::logic_error("unknown device state");
}
void Devices::Changed() {
    if (ChangeRevision == UINT64_MAX) throw std::overflow_error("device revision exhausted");
    ++ChangeRevision;
}
const DeviceRecord *Devices::Find(uint64_t id) const {
    const auto it = Records.find(id);
    return it == Records.end() ? nullptr : &it->second;
}
const DeviceRecord *Devices::FindAt(int64_t x, int64_t y) const {
    for (const auto &[id, record] : Records) {
        const auto &d = record.Value;
        if (x >= d.X && x < d.X + DeviceWidth && y >= d.Y && y < d.Y + DeviceHeight) return &record;
    }
    return nullptr;
}
bool Devices::Overlaps(int64_t x, int64_t y) const {
    for (const auto &[id, record] : Records) {
        const auto &d = record.Value;
        if (x < d.X + DeviceWidth && d.X < x + DeviceWidth &&
            y < d.Y + DeviceHeight && d.Y < y + DeviceHeight) return true;
    }
    return false;
}
std::vector<DeviceSnapshot> Devices::Snapshot() const {
    std::vector<DeviceSnapshot> result;
    result.reserve(Records.size());
    for (const auto &[id, record] : Records) result.push_back(record.Value);
    return result;
}
DeviceRef Devices::Create(int64_t x, int64_t y, uint64_t owner, items::ContainerId fuel) {
    if (!owner || !fuel.Value || x > INT64_MAX - DeviceWidth || y > INT64_MAX - DeviceHeight)
        throw std::invalid_argument("invalid device assembly");
    if (Count() == MaximumCount || Overlaps(x, y)) throw std::logic_error("unprepared device placement");
    if (NextId == UINT64_MAX) throw std::overflow_error("device identity exhausted");
    const DeviceRef ref{NextId++, 1};
    Records.emplace(ref.Id, DeviceRecord{{ref, owner, x, y}, fuel});
    Changed();
    return ref;
}
void Devices::Update(DeviceSnapshot value) {
    auto &record = Records.at(value.Ref.Id);
    const auto &old = record.Value;
    if (old.Ref != value.Ref || old.Owner != value.Owner || old.X != value.X || old.Y != value.Y)
        throw std::logic_error("invalid device update");
    if (value == old) return;
    if (value.Ref.Version == UINT64_MAX) throw std::overflow_error("device version exhausted");
    ++value.Ref.Version;
    record.Value = value;
    Changed();
}
void Devices::Remove(DeviceRef ref) {
    if (Records.at(ref.Id).Value.Ref != ref) throw std::logic_error("stale internal device removal");
    Records.erase(ref.Id);
    Changed();
}
void Devices::Clear() { Records.clear(); Changed(); }

DevicesState Devices::CaptureState() const {
    DevicesState state{NextId};
    for (const auto &[id, record] : Records) state.Records.push_back(record);
    return state;
}

void Devices::RestoreState(const DevicesState &state) {
    if (!state.NextId || state.Records.size() > MaximumCount)
        throw std::invalid_argument("invalid devices checkpoint");
    Devices candidate;
    for (const auto &record : state.Records) {
        const auto &d = record.Value;
        if (!d.Ref.Id || d.Ref.Id >= state.NextId || !d.Ref.Version || d.Ref.Version == UINT64_MAX
            || !d.Owner || !record.FuelContainer.Value || d.X > INT64_MAX - DeviceWidth
            || d.Y > INT64_MAX - DeviceHeight || int(d.State) < 0 || d.State > DeviceState::WaitingForCapacity
            || (!d.Enabled && d.Instance) || candidate.Overlaps(d.X, d.Y)
            || !candidate.Records.emplace(d.Ref.Id, record).second)
            throw std::invalid_argument("invalid saved device");
    }
    Records = std::move(candidate.Records);
    NextId = state.NextId;
    Changed();
}
} // namespace pixel
