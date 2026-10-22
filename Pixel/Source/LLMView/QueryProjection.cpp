#include "QueryProjection.h"
#include <algorithm>
#include <stdexcept>

namespace pixel::llm {
bool ValidRect(api::Rect rect) {
    return rect.Width && rect.Height
        && rect.Origin.X >= INT64_MIN + 16 && rect.Origin.Y >= INT64_MIN + 16
        && rect.Origin.X <= INT64_MAX - int64_t(rect.Width) - 16
        && rect.Origin.Y <= INT64_MAX - int64_t(rect.Height) - 16;
}

bool Contains(api::Rect outer, api::Rect inner) {
    return ValidRect(outer) && ValidRect(inner)
        && inner.Origin.X >= outer.Origin.X && inner.Origin.Y >= outer.Origin.Y
        && inner.Origin.X + inner.Width <= outer.Origin.X + outer.Width
        && inner.Origin.Y + inner.Height <= outer.Origin.Y + outer.Height;
}

namespace {
const api::ObservedCell &At(const api::ObservationSnapshot &snapshot, api::Cell cell) {
    return snapshot.Cells[size_t(cell.Y - snapshot.Scope.Origin.Y) * snapshot.Scope.Width
                          + size_t(cell.X - snapshot.Scope.Origin.X)];
}

void SampleRevision(QueryPage &page, uint64_t revision, bool first) {
    if (first) page.OldestSampledAtRevision = revision;
    page.OldestSampledAtRevision = std::min(page.OldestSampledAtRevision, revision);
    page.NewestSampledAtRevision = std::max(page.NewestSampledAtRevision, revision);
}

void ProjectCells(QueryPage &page, const api::ObservationSnapshot &snapshot, uint32_t offset) {
    const auto scope = page.Query.Scope;
    const auto count = std::min(scope.Width * scope.Height - offset, page.Query.PageSize);
    page.Cells.reserve(count);
    page.RequestedCells = count;
    uint32_t airSamples = 0;
    for (uint32_t i = offset; i < offset + count; ++i) {
        const api::Cell location{scope.Origin.X + i % scope.Width, scope.Origin.Y + i / scope.Width};
        const auto &cell = At(snapshot, location);
        CellSample value;
        value.Location = location;
        if (page.Query.Topic == api::Topic::Materials) {
            value.MaterialId = cell.MaterialId;
            value.SampledAtRevision = cell.MaterialSampledAtRevision;
        } else {
            value.AirKind = cell.AirKind;
            value.AirVelocity = cell.AirVelocity;
            value.SampledAtRevision = cell.WindSampledAtRevision;
            airSamples += cell.AirVelocity.has_value();
        }
        SampleRevision(page, value.SampledAtRevision, i == offset);
        page.ResidentCells += cell.MaterialId.has_value();
        page.Cells.push_back(value);
    }
    if (page.ResidentCells != page.RequestedCells) {
        page.Availability = api::Availability::Partial;
        page.Reason = "not_resident";
    } else if (page.Query.Topic == api::Topic::AirVelocity && airSamples == 0) {
        page.Availability = api::Availability::NotApplicable;
        page.Reason = "non_air";
    }
}

void ProjectWindSummary(QueryPage &page, const api::ObservationSnapshot &snapshot) {
    // Slice the authorized query first. Aggregating a larger box and filtering
    // afterwards would leak hidden cells into the averages and coverage.
    api::ObservationSnapshot selected;
    selected.Scope = page.Query.Scope;
    selected.Cells.reserve(selected.Scope.Width * selected.Scope.Height);
    for (uint32_t y = 0; y < selected.Scope.Height; ++y) {
        for (uint32_t x = 0; x < selected.Scope.Width; ++x) {
            const auto &cell = At(snapshot, {selected.Scope.Origin.X + x, selected.Scope.Origin.Y + y});
            SampleRevision(page, cell.WindSampledAtRevision, selected.Cells.empty());
            selected.Cells.push_back(cell);
        }
    }
    page.WindSummaries = api::SummarizeWind(selected);
    uint32_t airSamples = 0;
    for (const auto &bin : page.WindSummaries) {
        page.RequestedCells += bin.RequestedCells;
        page.ResidentCells += bin.ResidentCells;
        airSamples += bin.AirSamples;
    }
    if (page.RequestedCells != page.ResidentCells) {
        page.Availability = api::Availability::Partial;
        page.Reason = "not_resident";
    } else if (!airSamples) {
        page.Availability = api::Availability::NotApplicable;
        page.Reason = "non_air";
    }
}

bool PositionInside(api::Rect scope, const api::Position &position) {
    return Contains(scope, {position.AnchorCell, 1, 1});
}
} // namespace

QueryPage ProjectQuery(const api::ObservationSnapshot &snapshot, const ViewDescriptor &view,
                       QuerySpec query, uint32_t offset) {
    QueryPage page;
    page.View = view;
    page.Query = query;
    if (api::TopicSupport(query.Topic) == api::Availability::Unsupported) {
        page.Availability = api::Availability::Unsupported;
        page.Reason = "unsupported";
        page.Unit = "none";
        return page;
    }
    switch (query.Topic) {
    case api::Topic::Materials:
        page.Unit = "material_id";
        ProjectCells(page, snapshot, offset);
        break;
    case api::Topic::AirVelocity:
        page.Unit = "cell/second";
        page.Wind = snapshot.Wind;
        if (query.ResolutionCells == 16) ProjectWindSummary(page, snapshot);
        else ProjectCells(page, snapshot, offset);
        break;
    case api::Topic::Player:
        page.Unit = "cell";
        if (snapshot.Player && PositionInside(query.Scope, snapshot.Player->Location))
            page.Player = snapshot.Player;
        page.OldestSampledAtRevision = page.NewestSampledAtRevision = snapshot.ObjectSampledAtRevision;
        break;
    case api::Topic::Programs:
        page.Unit = "count";
        page.Programs = snapshot.Programs;
        page.OldestSampledAtRevision = page.NewestSampledAtRevision = snapshot.ObjectSampledAtRevision;
        break;
    case api::Topic::Runtime:
        page.Unit = "count";
        page.Programs = snapshot.Programs;
        page.Paused = snapshot.Paused;
        for (const auto &projectile : snapshot.Projectiles)
            if (PositionInside(query.Scope, projectile.Location)) page.Projectiles.push_back(projectile);
        page.OldestSampledAtRevision = page.NewestSampledAtRevision = snapshot.ObjectSampledAtRevision;
        break;
    default: throw std::logic_error("unknown supported observation topic");
    }
    return page;
}
} // namespace pixel::llm
