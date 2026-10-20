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

ForceVector Environment::SampleForce(CellCoord cell) const {
    return FieldResponse::AddWind(BaseForces.Get(cell.X, cell.Y), Wind.SampleForMaterial(cell));
}

void Environment::RestoreClock(double seconds, double remainder, bool deferredStep) {
    if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(remainder) || remainder < 0 || remainder > .25
        || (deferredStep && remainder < FieldResponse::StepSeconds))
        throw std::invalid_argument("invalid fixed simulation clock");
    SimulationSeconds = seconds;
    TimeRemainder = remainder;
    Wind.SetSimulationTime(seconds + (deferredStep ? FieldResponse::StepSeconds : 0));
    WindAdvancedForStep = deferredStep;
    WaitingForBudget = deferredStep;
}

uint32_t Environment::Tick(float duration, World &world, const std::function<void()> &onStep, bool singleStep) {
    PIXEL_PROFILE_SCOPE(Environment);
    if (!std::isfinite(duration) || duration <= 0)
        return 0;
    constexpr double stepSeconds = FieldResponse::StepSeconds;
    if (singleStep) {
        if (TimeRemainder < stepSeconds)
            TimeRemainder += stepSeconds;
    } else if (!WaitingForBudget) {
        TimeRemainder = std::min(TimeRemainder + double(duration), 0.25);
    }
    uint32_t completed = 0;
    VisitedCells = 0;
    ActiveRegions = 0;
    while (TimeRemainder >= stepSeconds) {
        if (!WindAdvancedForStep) {
            Wind.SetSimulationTime(SimulationSeconds);
            Wind.AdvanceBeforeMaterials(world.GetWindView(), float(stepSeconds));
            Wind.SetSimulationTime(SimulationSeconds + stepSeconds);
            WindAdvancedForStep = true;
        }
        try {
            StepMaterials(world);
        } catch (const WorldBudgetUnavailable &) {
            // Nothing has committed. Retry against the same advanced wind on
            // the next tick, after storage can be released at a safe boundary.
            world.GetMutableMap().Wake(world.GetSimulationBounds());
            TimeRemainder = std::min(TimeRemainder, .25);
            WaitingForBudget = true;
            return completed;
        }
        Wind.UpdateAfterMaterials(world.GetWindView(), DirtMovements, float(stepSeconds));
        TimeRemainder -= stepSeconds;
        SimulationSeconds += stepSeconds;
        Wind.SetSimulationTime(SimulationSeconds);
        WindAdvancedForStep = false;
        WaitingForBudget = false;
        performance::FrameProfiler::Count(performance::Counter::SimulationSteps);
        ++completed;
        if (onStep)
            onStep();
        if (singleStep)
            break;
    }
    return completed;
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
