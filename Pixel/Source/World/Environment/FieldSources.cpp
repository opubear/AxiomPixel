#include "FieldSources.h"
#include "World/WorldMap.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixel {
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

ForceVector FieldSources::Sample(CellCoord cell, WorldRect bounds) const {
    ForceVector result;
    if (!bounds.Contains(cell)) return result;
    for (const auto &source : Sources()) {
        if (!Fits(source.Spec, bounds)) continue;
        const auto center = source.Spec.Center.RelativeTo(cell);
        const auto dx = center[0] - .5, dy = center[1] - .5;
        if (dx * dx + dy * dy > source.Spec.Radius * source.Spec.Radius) continue;
        result.X += source.Spec.Acceleration.X;
        result.Y += source.Spec.Acceleration.Y;
    }
    return result;
}

void FieldSources::WakeActive(WorldMap &map, WorldRect bounds) const {
    for (const auto &source : Sources())
        if (Fits(source.Spec, bounds)) map.Wake(Bounds(source.Spec));
}

void FieldSources::Remove(size_t index, WorldMap &map) {
    map.Wake(Bounds(Values[index].Spec));
    std::move(Values.begin() + index + 1, Values.begin() + Count, Values.begin() + index);
    Values[--Count] = {};
    ++Version;
}

void FieldSources::CompleteStep(WorldMap &map, WorldRect bounds) {
    size_t index = 0;
    while (index < Count) {
        auto &source = Values[index];
        if (!Fits(source.Spec, bounds) || !map.TryGet(source.Spec.Center.Cell()) || --source.Remaining == 0)
            Remove(index, map);
        else
            ++index;
    }
}
} // namespace pixel
