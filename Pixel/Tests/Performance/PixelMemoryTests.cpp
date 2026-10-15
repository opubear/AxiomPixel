#include "Performance/MemoryUsage.h"
#include "World/World.h"
#include "World/Environment/Wind/LocalWindGeometry.h"
#include "World/Environment/Wind/ProceduralWindField.h"
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
    Require(initial.World.ObjectBytes == sizeof(World) - sizeof(ForceField) - sizeof(WindSimulation), "disjoint inline ownership");
    Require(initial.World.UsedHeapBytes == 64 * sizeof(CellState), "world cell payload");
    Require(initial.BaseForces.UsedHeapBytes == 64 * sizeof(ForceVector), "base force payload");
    Require(initial.Wind.UsedHeapBytes == 0, "disabled wind has no backend");
    Require(initial.Get(MemoryComponent::Total).UsedBytes() == sizeof(World) + 64 * (sizeof(CellState) + sizeof(ForceVector)), "whole world counted exactly once");

    world.GenerateTerrain(4);
    world.Tick(1.f / 60);
    const auto stepped = world.GetMemoryUsage();
    Require(stepped.World.ReservedBytes() > initial.World.ReservedBytes(), "simulation work buffers counted");
    Require(stepped.BaseForces == initial.BaseForces, "movement does not allocate base field");
    Require(stepped.World.ReservedBytes() >= stepped.World.UsedBytes(), "reserved includes used elements");

    WorldState snapshot(8, 8);
    world.PublishState(snapshot);
    Require(world.GetMemoryUsage().World == stepped.World, "publication snapshot is not double counted as world ownership");
    Require(snapshot.GetMemoryUsage().UsedHeapBytes == 64 * sizeof(CellState), "render-owned CPU snapshot independently measurable");

    world.GenerateTerrain(4);
    Require(world.GetMemoryUsage().World == initial.World, "reset releases simulation scratch allocations");
    World larger(16, 8);
    Require(larger.GetMemoryUsage().World.UsedHeapBytes == 2 * initial.World.UsedHeapBytes, "world scales with cell count");
    Require(larger.GetMemoryUsage().BaseForces.UsedHeapBytes == 2 * initial.BaseForces.UsedHeapBytes, "base field scales with cell count");
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
    Require(coarse.GetMemoryUsage().UsedHeapBytes == 12 * (5 * sizeof(WindVelocity) + 3 * sizeof(float) + sizeof(uint32_t)), "all coarse solver buffers counted");
    coarse.CoupleDirt(world.ReadState(), {}, 1.f / 60);
    Require(coarse.GetMemoryUsage().UsedHeapBytes == 12 * 56 + 16 * 3, "lazy occupied-column allocation counted");

    PixelWindSettings referenceSettings;
    referenceSettings.Ambient = {};
    referenceSettings.GustSpeed = 0;
    PixelWindField reference;
    reference.Reset(16, 12, referenceSettings);
    Require(reference.GetMemoryUsage().UsedHeapBytes == count * (10 * sizeof(double) + 5 * sizeof(uint8_t)), "all reference pressure and face arrays counted");

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
