#include "Runtime/Simulation/World/World.h"
#include "../Support/WorldAccess.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace pixel;
static_assert(std::is_const_v<WorldBuffers::ReadLease::element_type>);
static_assert(!std::is_copy_constructible_v<ForceField> && !std::is_copy_assignable_v<ForceField>);
static_assert(!std::is_copy_constructible_v<WindSimulation> && !std::is_copy_assignable_v<WindSimulation>);
static_assert(std::is_same_v<decltype(std::declval<const WorldBuffer &>().GetMap()), const WorldMap &>);
static_assert(std::is_same_v<decltype(std::declval<const WorldBuffer &>().GetWind()), const WindSimulation &>);

namespace {
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

void PublishedVersionsAndReuse() {
    World world(64, 64);
    world.SetCell(10, 10, {CellTypeId::Dirt});
    const auto source = world.InstallFieldSource(7, {WorldPosition::FromAbsolute(20.5, 20.5), 4, {3600, 0}, 10});
    Check(source.has_value(), "source fixture failed");
    world.PublishBuffers();
    auto first = world.ReadBuffers();
    const auto *firstSlot = first.get();
    const auto *firstStorage = first->GetMap().FindChunk({0, 0})->StorageIdentity();
    const auto force = first->SampleForce({20, 20});
    world.SetCell(10, 10, {CellTypeId::Water});
    world.SetBaseForce(20, 20, {123, 456});
    world.StopFieldSource(7, *source);
    Check(first->GetMap().Get({10, 10}).Type == CellTypeId::Dirt && first->SampleForce({20, 20}) == force,
          "writing simulation changed a published material/force/source version");
    Check(world.ReadBuffers() == first, "uncommitted writes changed the published version");
    world.PublishBuffers();
    auto second = world.ReadBuffers();
    Check(second != first && second->GetMap().Get({10, 10}).Type == CellTypeId::Water
          && second->SampleForce({20, 20}) == ForceVector{123, 456}, "publication mixed world versions");
    first.reset();
    world.SetCell(11, 10, {CellTypeId::Dirt});
    world.PublishBuffers();
    auto third = world.ReadBuffers();
    Check(third.get() == firstSlot && third->GetMap().FindChunk({0, 0})->StorageIdentity() == firstStorage,
          "released ping-pong slot did not reuse its chunk allocation");
    Check(third->GetMap().Get({10, 10}).Type == CellTypeId::Water && third->SampleForce({20, 20}) == ForceVector{123, 456},
          "reusing a slot lost prior material/force/source changes");
    world.SetCell(10, 10, {CellTypeId::Stone});
    world.PublishBuffers();
    Check(second->GetMap().Get({10, 10}).Type == CellTypeId::Water
          && third->GetMap().Get({11, 10}).Type == CellTypeId::Dirt,
          "slow readers were overwritten after both slots were used");
}

void ReaderOutlivesWorldAndReplacement() {
    WorldBuffers::ReadLease old;
    {
        World world(64, 64);
        world.SetCell(5, 5, {CellTypeId::Stone});
        world.PublishBuffers();
        old = world.ReadBuffers();
        auto &map = testing::WorldAccess::Map(world);
        map.Remove({0, 0});
        auto replacement = std::make_unique<WorldChunk>(CellCoord{0, 0});
        replacement->Data().Materials[5 * storage::ChunkEdge + 5] = CellTypeId::Water;
        replacement->Recount();
        map.Insert(std::move(replacement));
        world.PublishBuffers();
        Check(world.ReadBuffers()->GetMap().Get({5, 5}).Type == CellTypeId::Water, "replacement chunk was lost");
        world.SetCell(6, 5, {CellTypeId::Dirt});
        world.PublishBuffers();
        Check(world.ReadBuffers()->GetMap().Get({5, 5}).Type == CellTypeId::Water,
              "reused slot confused replacement chunk revisions");
    }
    Check(old->GetMap().Get({5, 5}).Type == CellTypeId::Stone && old->SampleForce({5, 5}).Y == 3600,
          "published version depends on the world's lifetime");
}

void EveryWindStrategyIsFrozen() {
    for (auto kind : {WindStrategyKind::Empty, WindStrategyKind::Coarse, WindStrategyKind::Reference,
                     WindStrategyKind::Procedural, WindStrategyKind::Hybrid, WindStrategyKind::Adaptive, WindStrategyKind::Lbm}) {
        World world(16, 16);
        WindStrategySettings settings;
        settings.Kind = kind;
        settings.Coarse.Columns = settings.Coarse.Rows = 4;
        world.ConfigureWindStrategy(16, settings);
        world.PublishBuffers();
        auto old = world.ReadBuffers();
        const auto velocity = old->SampleWind({8, 8});
        const auto force = old->SampleForce({8, 8});
        const auto revision = old->GetWind().GetRevision();
        world.AddWindImpulse(8, 8, 3, {10, 5});
        Check(world.StepEnvironment(0), "wind step failed");
        world.PublishBuffers();
        world.DisableWind();
        Check(old->SampleWind({8, 8}) == velocity && old->SampleForce({8, 8}) == force
              && old->GetWind().GetRevision() == revision && old->WindBounds().Height == 16,
              "wind backend mutated a retained version");
    }
}

void CopyBudgetIsAtomic() {
    const auto directory = std::filesystem::temp_directory_path()
        / ("pixel-buffer-budget-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path Directory; ~Cleanup() { std::filesystem::remove_all(Directory); } } cleanup{directory};
    World world;
    WorldStreamSettings settings;
    settings.Directory = directory;
    settings.WindowEdge = 64;
    settings.SurfaceY = 10000;
    settings.BudgetBytes = 6 * storage::ChunkReservedBytes + 8 * 1024 * 1024;
    world.EnableStreaming(settings, {0, 0, storage::ChunkEdge + 64, 64});
    world.SetCell(10, 10, {CellTypeId::Dirt});
    world.SetCell(storage::ChunkEdge + 10, 10, {CellTypeId::Dirt});
    world.PublishBuffers();
    const auto old = world.ReadBuffers();
    const auto available = settings.BudgetBytes - world.GetStreamer()->RetainedBytes();
    world.SetPublicationBytes(available - storage::ChunkReservedBytes - 32 * 1024);
    Check(!world.StepEnvironment(0), "copy pressure did not defer the material batch");
    Check(world.GetCell(10, 10).Type == CellTypeId::Dirt
          && world.GetCell(storage::ChunkEdge + 10, 10).Type == CellTypeId::Dirt,
          "copy budget failure committed only part of the material batch");
    Check(world.ReadBuffers() == old, "failed step published a new version");
    world.SetPublicationBytes(0);
    Check(world.StepEnvironment(0), "releasing budget did not resume the pending batch");
    world.PublishBuffers();
    Check(world.GetCell(10, 11).Type == CellTypeId::Dirt
          && world.GetCell(storage::ChunkEdge + 10, 11).Type == CellTypeId::Dirt
          && old->GetMap().Get({10, 10}).Type == CellTypeId::Dirt,
          "copy recovery lost moves or changed the previous frame");
    Check(world.GetStreamer()->RetainedBytes() <= settings.BudgetBytes, "retained versions exceeded the stream budget");
}

void SlowReadersCannotExhaustMetadataBudget() {
    const auto directory = std::filesystem::temp_directory_path()
        / ("pixel-reader-budget-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path Directory; ~Cleanup() { std::filesystem::remove_all(Directory); } } cleanup{directory};
    World world;
    WorldStreamSettings settings;
    settings.Directory = directory;
    settings.WindowEdge = 64;
    settings.SurfaceY = 10000;
    settings.BudgetBytes = 4 * storage::ChunkReservedBytes + 8 * 1024 * 1024;
    world.EnableStreaming(settings, {0, 0, 64, 64});
    world.PublishBuffers();
    std::vector<WorldBuffers::ReadLease> readers{world.ReadBuffers()};
    testing::WorldAccess::SaturateBudget(world, 128 * 1024);
    int frame = 0;
    for (; frame < 20; ++frame) {
        if (!world.StepEnvironment(frame / 60.0)) break;
        world.PublishBuffers();
        readers.push_back(world.ReadBuffers());
        Check(world.GetStreamer()->RetainedBytes() <= settings.BudgetBytes, "reader metadata exceeded the stream budget");
    }
    Check(frame < 20 && world.GetEnvironment().IsWaitingForBudget()
          && world.ReadBuffers() == readers.back(), "metadata pressure did not preserve the last complete version");
    readers.clear();
    Check(world.StepEnvironment(frame / 60.0), "released readers did not unblock version allocation");
    world.PublishBuffers();
}

void StreamingRetriesWithoutLosingCompletedChunks() {
    const auto directory = std::filesystem::temp_directory_path()
        / ("pixel-buffer-io-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path Directory; ~Cleanup() { std::filesystem::remove_all(Directory); } } cleanup{directory};
    WorldMap map;
    map.Insert(std::make_unique<WorldChunk>(CellCoord{0, 0}));
    bool writable = true;
    WorldStreamSettings settings;
    settings.Directory = directory;
    settings.WindowEdge = 64;
    settings.SurfaceY = 10000;
    settings.BudgetBytes = 6 * storage::ChunkReservedBytes + 8 * 1024 * 1024;
    WorldStreamer stream([&]() -> const WorldMap & { return map; }, [&]() -> WorldMap & {
        if (!writable) throw WorldBudgetUnavailable{};
        return map;
    }, settings);
    const WorldRect required{0, 0, 64, 64};
    stream.Update(required, {storage::ChunkEdge, 0, 64, 64});
    writable = false;
    bool denied = false;
    try { stream.Wait(); } catch (const WorldBudgetUnavailable &) { denied = true; }
    Check(denied && stream.PendingCount() != 0 && !map.FindChunk({1, 0}),
          "denied writer consumed a completed load");
    writable = true;
    stream.Wait();
    Check(stream.PendingCount() == 0 && map.FindChunk({1, 0}), "completed load did not survive writer retry");

    writable = false;
    const auto retained = stream.RetainedBytes();
    Check(!stream.ReserveExternalBytes(settings.BudgetBytes - retained + 1, required)
          && stream.RetainedBytes() == retained && map.FindChunk({1, 0}),
          "publication denial leaked its charge or evicted a sealed chunk");
}
} // namespace

int main() {
    try {
        PublishedVersionsAndReuse(); ReaderOutlivesWorldAndReplacement(); EveryWindStrategyIsFrozen(); CopyBudgetIsAtomic();
        SlowReadersCannotExhaustMetadataBudget();
        StreamingRetriesWithoutLosingCompletedChunks();
        std::cout << "World buffer publication and ownership passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
