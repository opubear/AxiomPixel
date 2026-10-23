#pragma once
#include "DeviceTypes.h"
#include "Items/Inventory.h"
#include <map>
#include <vector>

namespace pixel {
struct DeviceRecord { DeviceSnapshot Value; items::ContainerId FuelContainer; };
struct DevicesState {
    uint64_t NextId = 1;
    std::vector<DeviceRecord> Records;
};
class Devices {
  public:
    static constexpr size_t MaximumCount = 64;
    size_t Count() const { return Records.size(); }
    uint64_t Revision() const { return ChangeRevision; }
    const DeviceRecord *Find(uint64_t id) const;
    const DeviceRecord *FindAt(int64_t x, int64_t y) const;
    bool Overlaps(int64_t x, int64_t y) const;
    std::vector<DeviceSnapshot> Snapshot() const;
    DeviceRef Create(int64_t x, int64_t y, uint64_t owner, items::ContainerId fuel);
    void Update(DeviceSnapshot value);
    void Remove(DeviceRef reference);
    void Clear();
    DevicesState CaptureState() const;
    void RestoreState(const DevicesState &state);

  private:
    std::map<uint64_t, DeviceRecord> Records;
    uint64_t NextId = 1, ChangeRevision = 0;
    void Changed();
};
} // namespace pixel
