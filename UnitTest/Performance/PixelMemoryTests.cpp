#include "Performance/MemoryUsage.h"
#include "World/Environment/Wind/LocalWindGeometry.h"
#include "World/Environment/Wind/ProceduralWindField.h"
#include "World/World.h"
#include "World/WorldMap.h"
#include <iostream>
#include <stdexcept>

using namespace pixel;
using namespace pixel::performance;

namespace {
void Require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

void StorageAccounting() {
    struct Owner { std::vector<uint32_t> Values; } owner;
    owner.Values.reserve(19);
    owner.Values.resize(3);
    auto usage = MeasureObject(owner, owner.Values);
    Require(usage.ObjectBytes == sizeof(owner), "owner includes vector metadata once");
    Require(usage.UsedHeapBytes == 3 * sizeof(uint32_t), "size measures live elements");
    Require(usage.ReservedHeapBytes == owner.Values.capacity() * sizeof(uint32_t), "capacity measures retained allocation");

    owner.Values.clear();
    const auto empty = MeasureObject(owner, owner.Values);
    Require(empty.UsedHeapBytes == 0 && empty.ReservedHeapBytes == usage.ReservedHeapBytes, "clear retains allocation");

    MemoryUsage embedded{sizeof(Owner)};
    embedded.AddEmbedded(empty);
    Require(embedded == empty, "embedded object is not counted twice");

    MemoryUsage pointer{sizeof(void *)};
    pointer.AddOwned(empty);
    Require(pointer.ReservedBytes() == sizeof(void *) + empty.ReservedBytes(), "owned pointee includes concrete object");
}

void WorldPartitionAndReset() {
    World world(8, 8);
    const auto initial = world.GetMemoryUsage();
    const size_t chunkCount = world.GetMap().ChunkCount();
    const uint64_t forcePayloadBytes = chunkCount * storage::ChunkCells * sizeof(ForceVector);
    const uint64_t worldPayloadBytes = chunkCount * (sizeof(ChunkPayload) - storage::ChunkCells * sizeof(ForceVector));
    Require(chunkCount > 0, "finite world owns resident chunks");
    Require(initial.World.UsedHeapBytes >= worldPayloadBytes, "world accounts for each chunk's material and direction payload");
    Require(initial.BaseForces.UsedHeapBytes == forcePayloadBytes, "base forces scale with resident chunk cells");
    Require(initial.World.UsedHeapBytes + initial.BaseForces.UsedHeapBytes >= chunkCount * sizeof(ChunkPayload),
            "world and force owners account for the complete chunk payload");
    Require(initial.Get(MemoryComponent::Total) == initial.World + initial.BaseForces + initial.Wind,
            "whole world memory sums disjoint owners");
    Require(initial.Wind.UsedHeapBytes == 0, "disabled wind has no backend");

    world.GenerateTerrain(4);
    world.Tick(1.f / 60);
    const auto stepped = world.GetMemoryUsage();
    Require(stepped.World.ReservedBytes() >= stepped.World.UsedBytes(), "world reserved storage bounds usage");
    Require(stepped.BaseForces == initial.BaseForces, "movement does not allocate base field");

    WorldSnapshot snapshot(8, 8);
    world.PublishState(snapshot);
    Require(world.GetMemoryUsage().BaseForces == stepped.BaseForces, "publication does not change world force ownership");
    Require(snapshot.GetMemoryUsage().UsedHeapBytes >= 64 * sizeof(CellState), "render-owned CPU snapshot independently measurable");

    world.GenerateTerrain(4);
    const auto reset = world.GetMemoryUsage();
    Require(reset.BaseForces == initial.BaseForces, "reset preserves force storage for resident chunks");
    World larger(1025, 8);
    const size_t largerChunkCount = larger.GetMap().ChunkCount();
    const auto largerUsage = larger.GetMemoryUsage();
    Require(largerChunkCount > chunkCount, "larger extent creates the expected additional chunk");
    Require(largerUsage.BaseForces.UsedHeapBytes == largerChunkCount * storage::ChunkCells * sizeof(ForceVector),
            "base force ownership uses the actual resident chunk count");
    const uint64_t largerWorldPayloadBytes = largerChunkCount * (sizeof(ChunkPayload) - storage::ChunkCells * sizeof(ForceVector));
    Require(largerUsage.World.UsedHeapBytes >= largerWorldPayloadBytes && largerUsage.World.UsedHeapBytes + largerUsage.BaseForces.UsedHeapBytes >= largerChunkCount * sizeof(ChunkPayload),
            "world and force owners account for each resident chunk payload");
}

void SolverPayloads() {
    constexpr size_t count = 16 * 12;
    World world(16, 12);
    WindSettings settings;
    settings.Columns = 4;
    settings.Rows = 3;
    settings.Ambient = {};
    settings.GustSpeed = 0;
    WindField coarse;
    coarse.Reset(16, 12, settings);
    const auto edgeStateBytes = uint64_t(settings.Rows + settings.Columns) * sizeof(float);
    Require(coarse.GetMemoryUsage().UsedHeapBytes == 12 * (5 * sizeof(WindVelocity) + 3 * sizeof(float) + sizeof(uint32_t)) + edgeStateBytes,
            "all coarse solver and open-boundary buffers counted");
    coarse.CoupleDirt(world.ReadState(), {}, 1.f / 60);
    Require(coarse.GetMemoryUsage().UsedHeapBytes == 12 * 56 + edgeStateBytes + 16 * settings.Rows,
            "lazy occupied-column allocation counted");

    PixelWindSettings referenceSettings;
    referenceSettings.Ambient = {};
    referenceSettings.GustSpeed = 0;
    PixelWindField reference;
    reference.Reset(16, 12, referenceSettings);
    const uint64_t referenceBoundaryBytes = uint64_t(16 + 12) * sizeof(double);
    Require(reference.GetMemoryUsage().UsedHeapBytes == count * (10 * sizeof(double) + 5 * sizeof(uint8_t)) + referenceBoundaryBytes,
            "all reference pressure, face and open-boundary arrays counted");

    LbmWindSettings lbmSettings;
    lbmSettings.Ambient = {};
    lbmSettings.GustSpeed = 0;
    LbmWindField lbm(world.ReadState(), 12, lbmSettings);
    Require(lbm.GetMemoryUsage().UsedHeapBytes == count * (18 * sizeof(double) + sizeof(WindVelocity) + sizeof(uint8_t)), "both LBM distribution buffers counted");

    ProceduralWindField procedural(16, 12, settings);
    Require(procedural.GetMemoryUsage().UsedHeapBytes == count * sizeof(WindVelocity), "procedural impulses counted");
    LocalWindGeometry geometry(16, 12, {});
    Require(geometry.GetMemoryUsage().UsedHeapBytes == count * (sizeof(uint8_t) + sizeof(int32_t)), "empty geometry queue has no live entries");
    Require(geometry.GetMemoryUsage().ReservedHeapBytes >= count * 9, "reserved geometry queue counted");
}

void StrategySwitching() {
    World world(16, 12);
    WindStrategySettings settings;
    settings.Coarse.Columns = 4;
    settings.Coarse.Rows = 3;
    settings.Coarse.Ambient = settings.Reference.Ambient = settings.Adaptive.Flow.Ambient = settings.Lbm.Ambient = {};
    settings.Coarse.GustSpeed = settings.Reference.GustSpeed = settings.Adaptive.Flow.GustSpeed = settings.Lbm.GustSpeed = 0;
    const auto base = world.GetMemoryUsage().BaseForces;
    uint64_t emptyBytes = 0;
    for (auto kind : {WindStrategyKind::Empty, WindStrategyKind::Coarse, WindStrategyKind::Reference,
                      WindStrategyKind::Procedural, WindStrategyKind::Hybrid, WindStrategyKind::Adaptive, WindStrategyKind::Lbm}) {
        settings.Kind = kind;
        world.ConfigureWindStrategy(12, settings);
        const auto before = world.GetMemoryUsage();
        Require(before.BaseForces == base, "strategy switch preserves base allocation");
        Require(before.Wind.ReservedBytes() >= before.Wind.UsedBytes(), "strategy reserved storage bounds usage");
        if (kind == WindStrategyKind::Empty) {
            emptyBytes = before.Wind.ReservedBytes();
            Require(before.Wind.UsedHeapBytes > 0, "empty strategy still owns a small adapter");
        } else {
            Require(before.Wind.ReservedBytes() > emptyBytes, "active strategy accounts for numerical buffers");
        }

        world.Tick(1.f / 60);
        const auto after = world.GetMemoryUsage();
        Require(after.Wind.ReservedBytes() >= after.Wind.UsedBytes(), "updated strategy storage remains valid");
        Require(after.Get(MemoryComponent::Fields).ReservedBytes() == base.ReservedBytes() + after.Wind.ReservedBytes(), "fields sum uses disjoint owners");
        world.DisableWind();
        Require(world.GetMemoryUsage().Wind == MemoryUsage{sizeof(WindSimulation)}, "disable releases entire selected backend");
    }
}
} // namespace

int main() {
    try {
        StorageAccounting();
        WorldPartitionAndReset();
        SolverPayloads();
        StrategySwitching();
        std::cout << "World/force-field memory tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
