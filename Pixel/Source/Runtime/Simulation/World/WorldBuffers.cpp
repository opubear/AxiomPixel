#include "WorldBuffers.h"
#include "Environment/FieldResponse.h"
#include <algorithm>
#include <unordered_set>

namespace pixel {
WorldBuffer::WorldBuffer() : BaseForces(Map), Sources(Map) {}
WorldBuffer::WorldBuffer(uint32_t width, uint32_t height) : Map(width, height), BaseForces(Map), Sources(Map) {}

void WorldBuffer::ShareFrom(const WorldBuffer &source) {
    Map.ShareFrom(source.Map);
    BaseForces = ForceField(Map);
    Sources = source.Sources;
    Sources.Map = &Map;
    Wind.ShareFrom(source.Wind);
    Simulation = source.Simulation;
    WindRegion = source.WindRegion;
    WindEnabled = source.WindEnabled;
}

ForceVector WorldBuffer::SampleForce(CellCoord cell) const {
    return SampleForce(cell, Map.GetForce(cell));
}

ForceVector WorldBuffer::SampleForce(CellCoord cell, ForceVector base) const {
    auto result = FieldResponse::AddWind(base, Wind.SampleForMaterial(cell));
    const auto contribution = Sources.Sample(cell, Simulation);
    result.X += contribution.X;
    result.Y += contribution.Y;
    return result;
}

WindVelocity WorldBuffer::FieldVelocity(CellCoord cell) const {
    return FieldResponse::MovementVelocity(SampleForce(cell));
}

WindVelocity WorldBuffer::FieldVelocity(CellCoord cell, ForceVector base) const {
    return FieldResponse::MovementVelocity(SampleForce(cell, base));
}

WorldBuffers::WorldBuffers() { Slots[0].reset(new WorldBuffer); }
WorldBuffers::WorldBuffers(uint32_t width, uint32_t height) { Slots[0].reset(new WorldBuffer(width, height)); }

void WorldBuffers::SetWriteReservation(std::function<void(uint64_t, std::span<const CellCoord>)> reserve,
                                       std::function<void(uint64_t)> reserveVersion) {
    ReserveWrite = std::move(reserve);
    ReserveVersion = std::move(reserveVersion);
    Slots[Writer]->Map.SetWriteReservation(ReserveWrite);
}

WorldBuffer &WorldBuffers::Write() {
    if (!Sealed) return *Slots[Writer];
    const auto next = 1 - Writer;
    const bool replace = !Slots[next] || Slots[next].use_count() != 1;
    const auto metadataBytes = [](const WorldBuffer &buffer) {
        return sizeof(WorldBuffer) + buffer.Map.GetMemoryUsage().ReservedHeapBytes
            - buffer.Map.ChunkCount() * (storage::ChunkReservedBytes() + sizeof(ChunkPayload));
    };
    const auto targetBytes = metadataBytes(Current());
    const auto reusableBytes = replace ? 0 : metadataBytes(*Slots[next]);
    auto additional = targetBytes > reusableBytes ? targetBytes - reusableBytes : 0;
    if (replace && Slots[next] && Retired.size() == Retired.capacity())
        additional += std::max<size_t>(1, Retired.capacity()) * 2 * sizeof(Retired[0]);
    // This callback only checks capacity. Eviction here would re-enter Write()
    // while the published slot is still sealed.
    if (ReserveVersion) ReserveVersion(additional);
    if (replace) {
        if (Slots[next]) Retired.emplace_back(Slots[next]);
        Slots[next].reset(new WorldBuffer);
        Slots[next]->Map.SetWriteReservation(ReserveWrite);
    }
    Slots[next]->ShareFrom(*Slots[Writer]);
    Writer = next;
    Sealed = false;
    std::erase_if(Retired, [](const auto &entry) { return entry.expired(); });
    return *Slots[Writer];
}

void WorldBuffers::DiscardPublished() {
    Committed.reset();
    const auto other = 1 - Writer;
    if (Slots[other] && Slots[other].use_count() > 1) Retired.emplace_back(Slots[other]);
    Slots[other].reset();
    if (Slots[Writer].use_count() == 1) Sealed = false;
}

WorldBuffers::ReadLease WorldBuffers::Publish(WorldRect simulation, WorldRect wind, bool enabled) {
    if (Sealed && Current().Simulation == simulation && Current().WindRegion == wind && Current().WindEnabled == enabled) return Committed;
    auto &buffer = Write();
    buffer.Simulation = simulation;
    buffer.WindRegion = wind;
    buffer.WindEnabled = enabled;
    // Resolve the residency-derived cache while simulation still owns mutation.
    buffer.Sources.Revision();
    Committed = Slots[Writer];
    Sealed = true;
    ++Version;
    return Committed;
}

performance::MemoryUsage WorldBuffers::RetainedStorage(bool includeWind) const {
    std::unordered_set<const void *> payloads, winds;
    // Exclude the live simulation storage; World already accounts for it.
    Current().Map.UniqueStorage(payloads);
    winds.insert(Current().Wind.StorageIdentity());
    performance::MemoryUsage usage{0, Retired.size() * sizeof(Retired[0]), Retired.capacity() * sizeof(Retired[0])};
    const auto add = [&](const WorldBuffer &buffer) {
        performance::MemoryUsage retained{sizeof(WorldBuffer)};
        retained.AddEmbedded(buffer.Map.UniqueStorage(payloads));
        if (includeWind && winds.insert(buffer.Wind.StorageIdentity()).second)
            retained.AddEmbedded(buffer.Wind.GetMemoryUsage());
        usage.AddOwned(retained);
    };
    for (size_t i = 0; i < Slots.size(); ++i)
        if (i != Writer && Slots[i]) add(*Slots[i]);
    for (const auto &entry : Retired)
        if (const auto buffer = entry.lock()) add(*buffer);
    return usage;
}
} // namespace pixel
