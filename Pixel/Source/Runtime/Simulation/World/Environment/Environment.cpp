#include "Environment.h"
#include "FieldResponse.h"
#include "CellularMaterials.h"
#include "Dev/Debug/Performance/FrameProfiler.h"
#include "Runtime/Simulation/World/World.h"
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

} // namespace
performance::MemoryUsage Environment::GetMemoryUsage() const {
    auto usage = performance::MeasureObject(*this, Proposals, Changes, DirtMovements, Winners, WinnerEpochs);
    usage.AddEmbedded(GetWindSimulation().GetMemoryUsage());
    return usage;
}

ForceVector Environment::SampleForce(CellCoord cell, WorldRect bounds) const {
    return SampleForce(cell, bounds, GetBaseForceField().Get(cell.X, cell.Y));
}

ForceVector Environment::SampleForce(CellCoord cell, WorldRect bounds, ForceVector base) const {
    auto result = FieldResponse::AddWind(base, GetWindSimulation().SampleForMaterial(cell));
    const auto acceleration = GetFieldSources().Sample(cell, bounds);
    result.X += acceleration.X;
    result.Y += acceleration.Y;
    return result;
}

void Environment::Reset() {
    RequireSourceMutation();
    auto &buffer = Buffers->Write();
    buffer.BaseForces = ForceField(buffer.Map);
    const auto next = buffer.Sources.NextId, revision = buffer.Sources.Version + 1;
    buffer.Sources = FieldSources(buffer.Map);
    buffer.Sources.NextId = next;
    buffer.Sources.Version = revision;
    buffer.Wind.Clear();
    *this = Environment(*Buffers);
}

void Environment::RequireSourceMutation() const {
    if (WindAdvancedForStep)
        throw std::logic_error("field source mutation requires a committed environment step");
}

void Environment::SimulationDomainChanged() {
    RequireSourceMutation();
    // Source eligibility depends on the full disk fitting the active domain.
    // Publication must observe that change even when no wind backend is enabled.
    if (MutableSources().Size() != 0) ++MutableSources().Version;
}

std::optional<FieldSourceHandle> Environment::InstallFieldSource(World &world, uint64_t owner, const FieldSourceSpec &input) {
    RequireSourceMutation();
    const auto spec = FieldSources::Validate(owner, input);
    if (!MutableSources().HasCapacity() || !FieldSources::Fits(spec, world.GetSimulationBounds())
        || !world.TryGetCell(spec.Center.Cell().X, spec.Center.Cell().Y)) return std::nullopt;
    if (!MutableSources().NextId) throw std::overflow_error("field source identity exhausted");

    const FieldSourceHandle handle{MutableSources().NextId++};
    MutableSources().Values[MutableSources().Count++] = {handle, owner, spec, spec.Lifetime};
    ++MutableSources().Version;
    world.GetMutableMap().Wake(FieldSources::Bounds(spec));
    return handle;
}

bool Environment::UpdateFieldSource(World &world, uint64_t owner, FieldSourceHandle handle, const FieldSourceSpec &input) {
    RequireSourceMutation();
    const auto spec = FieldSources::Validate(owner, input);
    auto *source = MutableSources().Find(owner, handle);
    if (!source || source->Suspended || !FieldSources::Fits(spec, world.GetSimulationBounds())
        || !world.TryGetCell(spec.Center.Cell().X, spec.Center.Cell().Y)) return false;

    world.GetMutableMap().Wake(FieldSources::Bounds(source->Spec));
    source->Spec = spec;
    source->Remaining = spec.Lifetime;
    ++MutableSources().Version;
    world.GetMutableMap().Wake(FieldSources::Bounds(spec));
    return true;
}

bool Environment::StopFieldSource(World &world, uint64_t owner, FieldSourceHandle handle) {
    RequireSourceMutation();
    const auto *source = MutableSources().Find(owner, handle);
    if (!source) return false;
    MutableSources().Remove(size_t(source - MutableSources().Values.data()), world.GetMutableMap());
    return true;
}

void Environment::StopFieldSources(World &world, uint64_t owner) {
    RequireSourceMutation();
    size_t index = 0;
    while (index < MutableSources().Count)
        if (MutableSources().Values[index].Owner == owner)
            MutableSources().Remove(index, world.GetMutableMap());
        else
            ++index;
}

void Environment::SuspendFieldSources(World &world, uint64_t owner, bool suspended) {
    RequireSourceMutation();
    for (auto &source : std::span(MutableSources().Values.data(), MutableSources().Count)) {
        if (source.Owner != owner || source.Suspended == suspended) continue;
        source.Suspended = suspended;
        ++MutableSources().Version;
        world.GetMutableMap().Wake(FieldSources::Bounds(source.Spec));
    }
}

void Environment::RestoreFieldSources(World &world, const FieldSourcesState &state) {
    RequireSourceMutation();
    MutableSources().RestoreState(state, world.GetMutableMap());
}

void Environment::RestorePendingStep(double seconds, bool deferredStep) {
    GetWindSimulation().SetSimulationTime(seconds + (deferredStep ? FieldResponse::StepSeconds : 0));
    PendingSeconds = seconds;
    WindAdvancedForStep = deferredStep;
    WaitingForBudget = deferredStep;
}

bool Environment::TryStep(World &world, double committedSeconds) {
    PIXEL_PROFILE_SCOPE(Environment);
    constexpr double stepSeconds = FieldResponse::StepSeconds;
    VisitedCells = 0;
    ActiveRegions = 0;
    try {
        if (!WindAdvancedForStep) {
            PendingSeconds = committedSeconds;
            GetWindSimulation().SetSimulationTime(committedSeconds);
            GetWindSimulation().AdvanceBeforeMaterials(world.GetWindView(), float(stepSeconds));
            GetWindSimulation().SetSimulationTime(committedSeconds + stepSeconds);
            WindAdvancedForStep = true;
        } else if (PendingSeconds != committedSeconds) {
            throw std::logic_error("pending environment step changed its clock");
        }
        StepMaterials(world);
    } catch (const WorldBudgetUnavailable &) {
        if (!Buffers->Sealed) world.GetMutableMap().Wake(world.GetSimulationBounds());
        WaitingForBudget = true;
        return false;
    }
    GetWindSimulation().UpdateAfterMaterials(world.GetWindView(), DirtMovements, float(stepSeconds));
    MutableSources().CompleteStep(world.GetMutableMap(), world.GetSimulationBounds());
    WindAdvancedForStep = false;
    WaitingForBudget = false;
    performance::FrameProfiler::Count(performance::Counter::SimulationSteps);
    return true;
}

void Environment::ProposeRegion(World &world, WorldRect region) {
    auto &map = world.GetMutableMap();
    const auto bounds = world.GetSimulationBounds();
    const auto step = uint64_t(std::round(PendingSeconds * FieldResponse::StepsPerSecond));
    bool hasTransient = false;
    for (uint32_t row = 0; row < region.Height; ++row) {
        const CellCoord begin{region.X, region.Y + row};
        const auto materials = map.MaterialRow(begin, region.Width);
        const auto forces = map.ForceRow(begin, region.Width);
        for (uint32_t column = 0; column < region.Width; ++column) {
            const auto type = materials[column];
            if (type != CellTypeId::Dirt && type != CellTypeId::Water && !cellular::IsTransient(type))
                continue;
            const CellCoord source{begin.X + column, begin.Y};
            ForceDirection direction{0, 0};
            if (type == CellTypeId::Dirt) {
                direction = FieldResponse::MovementDirection(FieldResponse::MovementVelocity(SampleForce(source, bounds, forces[column])));
            } else {
                hasTransient |= cellular::IsTransient(type);
                const auto update = cellular::Evaluate(map, bounds, source, type, step, GetWindSimulation().SampleWorld(source));
                if (update.Material != type) {
                    AppendScratch(Changes, MaterialChange{source, type, update.Material}, world, *this);
                    continue;
                }
                if (update.Destination)
                    direction = {int32_t(update.Destination->X - source.X), int32_t(update.Destination->Y - source.Y)};
            }
            if (direction.X == 0 && direction.Y == 0)
                continue;
            const CellCoord destination{source.X + direction.X, source.Y + direction.Y};
            if (!bounds.Contains(destination))
                continue;
            const auto target = map.TryGetMaterial(destination);
            if (target && *target == CellTypes::Empty) {
                if (Proposals.empty())
                    ProposalDirection = direction;
                else if (ProposalDirection != direction)
                    SharedProposalDirection = false;
                AppendScratch(Proposals, MaterialMove{source, destination, type}, world, *this);
            }
        }
    }
    // A trapped or momentarily idle wisp still has a time-dependent rule.
    if (hasTransient)
        map.Wake(region);
}

void Environment::StepMaterials(World &world) {
    PIXEL_PROFILE_SCOPE(MaterialStep);
    Proposals.clear();
    Changes.clear();
    SharedProposalDirection = true;
    DirtMovements.clear();
    auto &map = world.GetMutableMap();
    MutableSources().WakeActive(map, world.GetSimulationBounds());
    const auto dynamicForces = GetWindSimulation().GetKind() == WindStrategyKind::Empty ? WorldRect{} : world.GetWindBounds();
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
    if (GetWindSimulation().NeedsMaterialMotion())
        std::sort(Proposals.begin(), Proposals.end(), [](const MaterialMove &a, const MaterialMove &b) {
            return CellCoordLess{}(a.Destination, b.Destination);
        });
    const auto windOrigin = world.GetWindView().Origin;
    for (const auto &move : Proposals) {
        if (move.Type == CellTypeId::Dirt && GetWindSimulation().NeedsMaterialMotion() && world.GetWindBounds().Contains(move.Destination)) {
            AppendScratch(DirtMovements, DirtMotion{uint32_t(move.Destination.X - windOrigin.X), uint32_t(move.Destination.Y - windOrigin.Y), {float(move.Destination.X - move.Source.X) * FieldResponse::StepsPerSecond, float(move.Destination.Y - move.Source.Y) * FieldResponse::StepsPerSecond}}, world, *this);
        }
    }
    map.CommitMaterialMoves(Proposals, Changes);
}

void Environment::ResolveConflicts(World &world, WorldRect bounds) {
    if (Proposals.empty())
        return;
    // Translation by one shared direction is injective: distinct sources cannot
    // compete for a destination. Gravity-dominated dense motion needs no table.
    if (SharedProposalDirection)
        return;
    if (Winners.Width() != bounds.Width || Winners.Height() != bounds.Height) {
        world.ReserveSimulationBytes(ScratchBytes(*this)
            + 2 * memory::ChunkedGrid<uint32_t>::AllocationBytes(bounds.Width, bounds.Height));
        {
            memory::ChunkedGrid<uint32_t> winners(bounds.Width, bounds.Height), epochs(bounds.Width, bounds.Height);
            Winners.swap(winners);
            WinnerEpochs.swap(epochs);
        }
        world.ReserveSimulationBytes(ScratchBytes(*this));
        Epoch = 0;
    }
    if (++Epoch == 0) {
        WinnerEpochs.Fill(0);
        Epoch = 1;
    }
    const auto index = [&](CellCoord cell) {
        return Winners.Address(uint32_t(cell.X - bounds.X), uint32_t(cell.Y - bounds.Y));
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
