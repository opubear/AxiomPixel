#include "Observations.h"
#include <algorithm>
#include <stdexcept>

namespace pixel::api {
std::string_view DeviceStateReason(DeviceState state) {
    switch (state) {
    case DeviceState::Stopped: return "disabled";
    case DeviceState::Running: return "paid_cycle_active";
    case DeviceState::NeedsFuel: return "needs_coal";
    case DeviceState::WaitingForEnvironment: return "device_footprint_blocked";
    case DeviceState::OutsideActiveArea: return "outside_active_or_resident_area";
    case DeviceState::WaitingForCapacity: return "waiting_for_execution_or_field_capacity";
    }
    throw std::logic_error("unknown device observation state");
}

uint64_t GameplayObservation::RetainedBytes() const {
    uint64_t bytes = sizeof(*this) + Stamp.WorldId.capacity() + 1
        + Inventories.capacity() * sizeof(InventoryObservation) + Devices.capacity() * sizeof(DeviceObservation)
        + Processes.capacity() * sizeof(ProcessObservation) + FieldSources.capacity() * sizeof(FieldSourceObservation)
        + Protections.capacity() * sizeof(authority::Protection) + WorldWill.RetainedBytes() - sizeof(WorldWillMemory);
    for (const auto &inventory : Inventories) bytes += inventory.RetainedBytes() - sizeof(InventoryObservation);
    return bytes;
}

bool ValidPrayerText(std::string_view text) {
    if (text.size() > WorldWillMemory::PrayerBytes) return false;
    for (size_t index = 0; index < text.size();) {
        const auto first = static_cast<unsigned char>(text[index++]);
        uint32_t scalar = first, minimum = 0;
        size_t trailing = 0;
        if (first >= 0xc2 && first <= 0xdf) { scalar = first & 0x1f; trailing = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { scalar = first & 0x0f; trailing = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { scalar = first & 0x07; trailing = 3; minimum = 0x10000; }
        else if (first >= 0x80) return false;
        if (trailing > text.size() - index) return false;
        for (size_t byte = 0; byte < trailing; ++byte) {
            const auto next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return false;
        if ((scalar < 0x20 && scalar != '\n' && scalar != '\r' && scalar != '\t')
            || (scalar >= 0x7f && scalar <= 0x9f)) return false;
    }
    return true;
}

uint64_t WorldWillMemory::RetainedBytes() const {
    uint64_t bytes = sizeof(*this) + Records.capacity() * sizeof(PrayerRecord);
    for (const auto &record : Records)
        bytes += record.Explanation.capacity() + record.Prayer.capacity()
            + record.ObservedAt.WorldId.capacity() + record.ResolvedAt.WorldId.capacity() + 4;
    return bytes;
}

uint64_t ObservationSnapshot::RetainedBytes() const {
    return sizeof(*this) + Stamp.WorldId.capacity() + 1
        + Cells.capacity() * sizeof(ObservedCell) + Projectiles.capacity() * sizeof(ProjectileObservation)
        + Gameplay.RetainedBytes() - sizeof(GameplayObservation)
        + Knowledge.RetainedBytes() - sizeof(knowledge::Snapshot);
}

namespace {
int64_t BinStart(int64_t value) { return (value / 16 - (value % 16 < 0)) * 16; }

void Accumulate(WindSummary &summary, const ObservedCell &cell, std::array<double, 2> &sum) {
    ++summary.RequestedCells;
    if (!cell.MaterialId) {
        ++summary.UnknownCells;
        return;
    }
    ++summary.ResidentCells;
    if (!cell.AirVelocity) {
        ++summary.NonAirCells;
        return;
    }
    ++summary.AirSamples;
    for (size_t axis = 0; axis < 2; ++axis) sum[axis] += (*cell.AirVelocity)[axis];
    if (summary.AirSamples == 1) summary.OldestSampledAtRevision = cell.WindSampledAtRevision;
    summary.OldestSampledAtRevision = std::min(summary.OldestSampledAtRevision, cell.WindSampledAtRevision);
    summary.NewestSampledAtRevision = std::max(summary.NewestSampledAtRevision, cell.WindSampledAtRevision);
    summary.OutsideDomainSamples += cell.AirKind == AirSampleKind::OutsideDomain;
    summary.EmptyBackendSamples += cell.AirKind == AirSampleKind::EmptyBackend;
    summary.DisabledSamples += cell.AirKind == AirSampleKind::Disabled;
}
} // namespace

std::vector<WindSummary> SummarizeWind(const ObservationSnapshot &snapshot) {
    const auto &scope = snapshot.Scope;
    if (!scope.Width || !scope.Height || scope.Width > 64 || scope.Height > 64
        || scope.Origin.X < INT64_MIN + 16 || scope.Origin.Y < INT64_MIN + 16
        || scope.Origin.X > INT64_MAX - int64_t(scope.Width) - 16
        || scope.Origin.Y > INT64_MAX - int64_t(scope.Height) - 16
        || snapshot.Cells.size() != size_t(scope.Width) * scope.Height)
        throw std::invalid_argument("invalid bounded observation snapshot");
    std::vector<WindSummary> result;
    result.reserve(25); // A misaligned 64-cell side intersects at most five bins.
    const auto right = scope.Origin.X + scope.Width, bottom = scope.Origin.Y + scope.Height;
    for (auto y = BinStart(scope.Origin.Y); y < bottom; y += 16) {
        for (auto x = BinStart(scope.Origin.X); x < right; x += 16) {
            WindSummary summary;
            const auto left = std::max(x, scope.Origin.X), top = std::max(y, scope.Origin.Y);
            summary.Scope = {{left, top}, uint32_t(std::min(x + 16, right) - left), uint32_t(std::min(y + 16, bottom) - top)};
            std::array<double, 2> sum{};
            for (uint32_t row = 0; row < summary.Scope.Height; ++row)
                for (uint32_t col = 0; col < summary.Scope.Width; ++col)
                    Accumulate(summary, snapshot.Cells[size_t(top + row - scope.Origin.Y) * scope.Width
                        + size_t(left + col - scope.Origin.X)], sum);
            summary.Status = summary.UnknownCells ? Availability::Partial
                : summary.AirSamples ? Availability::Available : Availability::NotApplicable;
            if (summary.AirSamples)
                summary.MeanVelocityCellsPerSecond = {sum[0] / summary.AirSamples, sum[1] / summary.AirSamples};
            result.push_back(summary);
        }
    }
    return result;
}
} // namespace pixel::api
