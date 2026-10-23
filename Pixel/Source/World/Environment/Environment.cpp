#include "Environment.h"
#include "FieldResponse.h"
#include "Performance/FrameProfiler.h"
#include "World/World.h"
#include <algorithm>
#include <cmath>

namespace pixel {
namespace {
uint64_t ScratchBytes(const Environment &environment) {
    return environment.GetMemoryUsage().ReservedHeapBytes - environment.GetWindSimulation().GetMemoryUsage().ReservedHeapBytes;
}

template <class T>
void AppendScratch(axiom::Array<T> &values, T value, World &world, const Environment &environment) {
    if (values.size() == values.capacity()) {
        const auto capacity = std::max<size_t>(64, values.capacity() * 2);
        world.ReserveSimulationBytes(ScratchBytes(environment) + capacity * sizeof(T));
        {
            axiom::Array<T> next;
            next.reserve(capacity);
            next.insert(next.end(), values.begin(), values.end());
            values.swap(next);
        }
        // Destroying the old allocator releases its cached allocation pools.
        world.ReserveSimulationBytes(ScratchBytes(environment));
    }
    values.push_back(value);
}

std::optional<CellCoord> WaterDestination(const WorldMap &map, WorldRect bounds, CellCoord source) {
    const auto is = [&](CellCoord cell, CellTypeId type) {
        const auto value = bounds.Contains(cell) ? map.TryGet(cell) : std::nullopt;
        return value && value->Type == type;
    };
    const CellCoord below{source.X, source.Y + 1};
    if (is(below, CellTypeId::Air))
        return below;
    // A side step must lead downhill, or have a second water cell providing
    // head above the source. A lone surface cell cannot oscillate sideways.
    const bool hasHead = is({source.X, source.Y - 1}, CellTypeId::Water);
    const int first = (uint64_t(source.X) ^ uint64_t(source.Y)) & 1 ? -1 : 1;
    for (const int dx : {first, -first}) {
        const CellCoord side{source.X + dx, source.Y};
        const bool dropsFromSide = is({side.X, side.Y + 1}, CellTypeId::Air);
        // Do not follow a supporting water cell into its lateral vacancy in
        // the same synchronous step. That translates whole columns forever.
        if (dropsFromSide && is(below, CellTypeId::Water))
            continue;
        if (is(side, CellTypeId::Air) && (hasHead || dropsFromSide))
            return side;
    }
    return std::nullopt;
}
} // namespace
performance::MemoryUsage Environment::GetMemoryUsage() const {
    auto usage = performance::MeasureObject(*this, Proposals, DirtMovements, Winners, WinnerEpochs);
    usage.AddEmbedded(Wind.GetMemoryUsage());
    return usage;
}

ForceVector Environment::SampleForce(CellCoord cell, WorldRect bounds) const {
    auto result = FieldResponse::AddWind(BaseForces.Get(cell.X, cell.Y), Wind.SampleForMaterial(cell));
    const auto acceleration = Sources.Sample(cell, bounds);
    result.X += acceleration.X;
    result.Y += acceleration.Y;
    return result;
}

void Environment::Reset(WorldMap &map) {
    RequireSourceMutation();
    const auto next = Sources.NextId, revision = Sources.Version + 1;
    *this = Environment(map);
    Sources.NextId = next;
    Sources.Version = revision;
}

void Environment::RequireSourceMutation() const {
    if (WindAdvancedForStep)
        throw std::logic_error("field source mutation requires a committed environment step");
}

void Environment::SimulationDomainChanged() {
    RequireSourceMutation();
    // Source eligibility depends on the full disk fitting the active domain.
    // Publication must observe that change even when no wind backend is enabled.
    if (Sources.Size() != 0) ++Sources.Version;
}

std::optional<FieldSourceHandle> Environment::InstallFieldSource(World &world, uint64_t owner, const FieldSourceSpec &input) {
    RequireSourceMutation();
    const auto spec = FieldSources::Validate(owner, input);
    if (!Sources.HasCapacity() || !FieldSources::Fits(spec, world.GetSimulationBounds())
        || !world.TryGetCell(spec.Center.Cell().X, spec.Center.Cell().Y)) return std::nullopt;
    if (!Sources.NextId) throw std::overflow_error("field source identity exhausted");

    const FieldSourceHandle handle{Sources.NextId++};
    Sources.Values[Sources.Count++] = {handle, owner, spec, spec.Lifetime};
    ++Sources.Version;
    world.GetMutableMap().Wake(FieldSources::Bounds(spec));
    return handle;
}

bool Environment::UpdateFieldSource(World &world, uint64_t owner, FieldSourceHandle handle, const FieldSourceSpec &input) {
    RequireSourceMutation();
    const auto spec = FieldSources::Validate(owner, input);
    auto *source = Sources.Find(owner, handle);
    if (!source || source->Suspended || !FieldSources::Fits(spec, world.GetSimulationBounds())
        || !world.TryGetCell(spec.Center.Cell().X, spec.Center.Cell().Y)) return false;

    world.GetMutableMap().Wake(FieldSources::Bounds(source->Spec));
    source->Spec = spec;
    source->Remaining = spec.Lifetime;
    ++Sources.Version;
    world.GetMutableMap().Wake(FieldSources::Bounds(spec));
    return true;
}

bool Environment::StopFieldSource(World &world, uint64_t owner, FieldSourceHandle handle) {
    RequireSourceMutation();
    const auto *source = Sources.Find(owner, handle);
    if (!source) return false;
    Sources.Remove(size_t(source - Sources.Values.data()), world.GetMutableMap());
    return true;
}

void Environment::StopFieldSources(World &world, uint64_t owner) {
    RequireSourceMutation();
    size_t index = 0;
    while (index < Sources.Count)
        if (Sources.Values[index].Owner == owner)
            Sources.Remove(index, world.GetMutableMap());
        else
            ++index;
}

void Environment::SuspendFieldSources(World &world, uint64_t owner, bool suspended) {
    RequireSourceMutation();
    for (auto &source : std::span(Sources.Values.data(), Sources.Count)) {
        if (source.Owner != owner || source.Suspended == suspended) continue;
        source.Suspended = suspended;
        ++Sources.Version;
        world.GetMutableMap().Wake(FieldSources::Bounds(source.Spec));
    }
}

void Environment::RestoreFieldSources(World &world, const FieldSourcesState &state) {
    RequireSourceMutation();
    Sources.RestoreState(state, world.GetMutableMap());
}

void Environment::RestorePendingStep(double seconds, bool deferredStep) {
    Wind.SetSimulationTime(seconds + (deferredStep ? FieldResponse::StepSeconds : 0));
    PendingSeconds = seconds;
    WindAdvancedForStep = deferredStep;
    WaitingForBudget = deferredStep;
}

bool Environment::TryStep(World &world, double committedSeconds) {
    PIXEL_PROFILE_SCOPE(Environment);
    constexpr double stepSeconds = FieldResponse::StepSeconds;
    VisitedCells = 0;
    ActiveRegions = 0;
    if (!WindAdvancedForStep) {
        PendingSeconds = committedSeconds;
        Wind.SetSimulationTime(committedSeconds);
        Wind.AdvanceBeforeMaterials(world.GetWindView(), float(stepSeconds));
        Wind.SetSimulationTime(committedSeconds + stepSeconds);
        WindAdvancedForStep = true;
    } else if (PendingSeconds != committedSeconds) {
        throw std::logic_error("pending environment step changed its clock");
    }
    try {
        StepMaterials(world);
    } catch (const WorldBudgetUnavailable &) {
        world.GetMutableMap().Wake(world.GetSimulationBounds());
        WaitingForBudget = true;
        return false;
    }
    Wind.UpdateAfterMaterials(world.GetWindView(), DirtMovements, float(stepSeconds));
    Sources.CompleteStep(world.GetMutableMap(), world.GetSimulationBounds());
    WindAdvancedForStep = false;
    WaitingForBudget = false;
    performance::FrameProfiler::Count(performance::Counter::SimulationSteps);
    return true;
}

void Environment::ProposeRegion(World &world, WorldRect region) {
    auto &map = world.GetMutableMap();
    const auto bounds = world.GetSimulationBounds();
    for (uint32_t row = 0; row < region.Height; ++row) {
        const CellCoord begin{region.X, region.Y + row};
        const auto materials = map.MaterialRow(begin, region.Width);
        for (uint32_t column = 0; column < region.Width; ++column) {
            const auto type = materials[column];
            if (type != CellTypeId::Dirt && type != CellTypeId::Water)
                continue;
            const CellCoord source{begin.X + column, begin.Y};
            ForceDirection direction{0, 0};
            if (type == CellTypeId::Water) {
                const auto destination = WaterDestination(map, bounds, source);
                if (destination)
                    direction = {int32_t(destination->X - source.X), int32_t(destination->Y - source.Y)};
            } else {
                direction = FieldResponse::MovementDirection(world.GetFieldVelocity(source.X, source.Y));
            }
            if (direction.X == 0 && direction.Y == 0)
                continue;
            const CellCoord destination{source.X + direction.X, source.Y + direction.Y};
            if (!bounds.Contains(destination))
                continue;
            const auto target = map.TryGet(destination);
            if (target && target->Type == CellTypes::Empty) {
                if (Proposals.empty())
                    ProposalDirection = direction;
                else if (ProposalDirection != direction)
                    SharedProposalDirection = false;
                AppendScratch(Proposals, MaterialMove{source, destination, type}, world, *this);
            }
        }
    }
}

void Environment::StepMaterials(World &world) {
    PIXEL_PROFILE_SCOPE(MaterialStep);
    Proposals.clear();
    SharedProposalDirection = true;
    DirtMovements.clear();
    auto &map = world.GetMutableMap();
    Sources.WakeActive(map, world.GetSimulationBounds());
    const auto dynamicForces = Wind.GetKind() == WindStrategyKind::Empty ? WorldRect{} : world.GetWindBounds();
    const auto regions = map.TakeActiveRegions(world.GetSimulationBounds(), dynamicForces);
    ActiveRegions += regions.size();
    performance::FrameProfiler::Count(performance::Counter::ActiveRegions, regions.size());
    for (const auto region : regions) {
        const auto cells = uint64_t(region.Width) * region.Height;
        VisitedCells += cells;
        performance::FrameProfiler::Count(performance::Counter::MaterialCells, cells);
        ProposeRegion(world, region);
    }

    ResolveConflicts(world, world.GetSimulationBounds());
    if (Wind.NeedsMaterialMotion())
        std::sort(Proposals.begin(), Proposals.end(), [](const MaterialMove &a, const MaterialMove &b) {
            return CellCoordLess{}(a.Destination, b.Destination);
        });
    const auto windOrigin = world.GetWindView().Origin;
    for (const auto &move : Proposals) {
        if (move.Type == CellTypeId::Dirt && Wind.NeedsMaterialMotion() && world.GetWindBounds().Contains(move.Destination)) {
            AppendScratch(DirtMovements, DirtMotion{uint32_t(move.Destination.X - windOrigin.X), uint32_t(move.Destination.Y - windOrigin.Y), {float(move.Destination.X - move.Source.X) * FieldResponse::StepsPerSecond, float(move.Destination.Y - move.Source.Y) * FieldResponse::StepsPerSecond}}, world, *this);
        }
    }
    map.CommitMaterialMoves(Proposals);
}

void Environment::ResolveConflicts(World &world, WorldRect bounds) {
    if (Proposals.empty())
        return;
    // Translation by one shared direction is injective: distinct sources cannot
    // compete for a destination. Gravity-dominated dense motion needs no table.
    if (SharedProposalDirection)
        return;
    const auto cells = size_t(bounds.Width) * bounds.Height;
    if (Winners.size() != cells) {
        world.ReserveSimulationBytes(ScratchBytes(*this) + cells * 2 * sizeof(uint32_t));
        {
            axiom::Array<uint32_t> winners(cells), epochs(cells);
            Winners.swap(winners);
            WinnerEpochs.swap(epochs);
        }
        world.ReserveSimulationBytes(ScratchBytes(*this));
        Epoch = 0;
    }
    if (++Epoch == 0) {
        std::fill(WinnerEpochs.begin(), WinnerEpochs.end(), 0);
        Epoch = 1;
    }
    const auto index = [&](CellCoord cell) {
        return size_t(cell.Y - bounds.Y) * bounds.Width + size_t(cell.X - bounds.X);
    };
    for (uint32_t i = 0; i < Proposals.size(); ++i) {
        const auto target = index(Proposals[i].Destination);
        if (WinnerEpochs[target] != Epoch || CellCoordLess{}(Proposals[i].Source, Proposals[Winners[target]].Source)) {
            Winners[target] = i;
            WinnerEpochs[target] = Epoch;
        }
    }
    size_t accepted = 0;
    for (size_t i = 0; i < Proposals.size(); ++i)
        if (Winners[index(Proposals[i].Destination)] == i)
            Proposals[accepted++] = Proposals[i];
    Proposals.resize(accepted);
}
} // namespace pixel
