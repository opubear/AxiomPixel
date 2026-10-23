#include "FieldSources.h"
#include "World/WorldMap.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixel {
void FieldSources::RestoreState(const FieldSourcesState &state, WorldMap &map) {
    if (!state.NextId || state.Sources.size() > Capacity)
        throw std::invalid_argument("invalid field sources checkpoint");
    uint64_t previous = 0;
    for (const auto &source : state.Sources) {
        Validate(source.Owner, source.Spec);
        if (source.Handle.Id <= previous || source.Handle.Id >= state.NextId
            || !source.Remaining || source.Remaining > source.Spec.Lifetime)
            throw std::invalid_argument("invalid saved field source identity or lifetime");
        previous = source.Handle.Id;
    }
    for (const auto &source : Sources()) map.Wake(Bounds(source.Spec));
    Values = {};
    Count = state.Sources.size();
    std::copy(state.Sources.begin(), state.Sources.end(), Values.begin());
    NextId = state.NextId;
    Resident = {};
    CachedSourceVersion = CachedResidencyVersion = 0;
    ++Version;
    for (const auto &source : Sources()) map.Wake(Bounds(source.Spec));
}

FieldSourceSpec FieldSources::Validate(uint64_t owner, FieldSourceSpec spec) {
    if (!owner || !std::isfinite(spec.Radius) || spec.Radius <= 0 || spec.Radius > 16
        || !std::isfinite(spec.Acceleration.X) || !std::isfinite(spec.Acceleration.Y)
        || std::hypot(double(spec.Acceleration.X), double(spec.Acceleration.Y)) > 36000
        || !spec.Lifetime || spec.Lifetime > 3600)
        throw std::invalid_argument("invalid field source specification");
    spec.Center.Normalize();
    return spec;
}

bool FieldSources::Fits(const FieldSourceSpec &spec, WorldRect bounds) {
    if (bounds.Empty()) return false;
    const auto center = spec.Center.RelativeTo({bounds.X, bounds.Y});
    return center[0] >= spec.Radius && center[1] >= spec.Radius
        && center[0] + spec.Radius <= bounds.Width && center[1] + spec.Radius <= bounds.Height;
}

WorldRect FieldSources::Bounds(const FieldSourceSpec &spec) {
    const auto cell = spec.Center.Cell();
    const auto center = spec.Center.RelativeTo(cell);
    const auto left = int64_t(std::floor(center[0] - spec.Radius));
    const auto top = int64_t(std::floor(center[1] - spec.Radius));
    const auto right = int64_t(std::floor(center[0] + spec.Radius));
    const auto bottom = int64_t(std::floor(center[1] + spec.Radius));
    return {cell.X + left, cell.Y + top, uint32_t(right - left + 1), uint32_t(bottom - top + 1)};
}

FieldSourceSnapshot *FieldSources::Find(uint64_t owner, FieldSourceHandle handle) {
    for (auto &source : std::span(Values.data(), Count))
        if (source.Handle == handle && source.Owner == owner) return &source;
    return nullptr;
}

void FieldSources::RefreshResidency() const {
    const auto residency = Map->GetResidencyVersion();
    if (CachedSourceVersion == Version && CachedResidencyVersion == residency) return;
    bool changed = false;
    for (size_t index = 0; index < Count; ++index) {
        const bool resident = Map->TryGet(Values[index].Spec.Center.Cell()).has_value();
        changed |= Resident[index] != resident;
        Resident[index] = resident;
    }
    if (changed && CachedSourceVersion == Version) ++Version;
    CachedSourceVersion = Version;
    CachedResidencyVersion = residency;
}

uint64_t FieldSources::Revision() const {
    RefreshResidency();
    return Version;
}

bool FieldSources::Active(size_t index, WorldRect bounds) const {
    const auto &source = Values[index];
    return Resident[index] && !source.Suspended && Fits(source.Spec, bounds);
}

ForceVector FieldSources::Sample(CellCoord cell, WorldRect bounds) const {
    ForceVector result;
    if (!bounds.Contains(cell)) return result;
    RefreshResidency();
    for (size_t index = 0; index < Count; ++index) {
        const auto &source = Values[index];
        const auto center = source.Spec.Center.RelativeTo(cell);
        const auto dx = center[0] - .5, dy = center[1] - .5;
        if (dx * dx + dy * dy > source.Spec.Radius * source.Spec.Radius) continue;
        if (!Active(index, bounds)) continue;
        result.X += source.Spec.Acceleration.X;
        result.Y += source.Spec.Acceleration.Y;
    }
    return result;
}

void FieldSources::WakeActive(WorldMap &map, WorldRect bounds) const {
    RefreshResidency();
    for (size_t index = 0; index < Count; ++index)
        if (Active(index, bounds)) map.Wake(Bounds(Values[index].Spec));
}

void FieldSources::Remove(size_t index, WorldMap &map) {
    map.Wake(Bounds(Values[index].Spec));
    std::move(Values.begin() + index + 1, Values.begin() + Count, Values.begin() + index);
    std::move(Resident.begin() + index + 1, Resident.begin() + Count, Resident.begin() + index);
    Values[--Count] = {};
    Resident[Count] = false;
    ++Version;
}

void FieldSources::CompleteStep(WorldMap &map, WorldRect bounds) {
    RefreshResidency();
    size_t index = 0;
    while (index < Count) {
        auto &source = Values[index];
        // Frozen sources retain their identity and capacity reservation. There
        // is no elapsed-world-time catch-up when their region becomes active.
        if (!Active(index, bounds)) {
            ++index;
            continue;
        }
        if (--source.Remaining == 0)
            Remove(index, map);
        else
            ++index;
    }
}
} // namespace pixel
