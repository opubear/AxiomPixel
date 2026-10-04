#include "World/World.h"
#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>

namespace {
void Check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
bool IsDirt(const pixel::World &world, uint32_t x, uint32_t y) {
    return world.GetCell(x, y).Type == pixel::CellTypes::Dirt;
}
size_t DirtCount(const pixel::World &world) {
    const auto cells = world.ReadState().Cells;
    return std::count_if(cells.begin(), cells.end(), [](auto cell) { return cell.Type == pixel::CellTypes::Dirt; });
}
constexpr std::array<pixel::ForceDirection, 8> Directions{{{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};

void MovementDirections() {
    for (const auto direction : Directions) {
        pixel::World world(7, 7);
        world.FillForceDirection(direction);
        world.PlaceDirt(3, 3);
        world.Tick(1.f / 60);
        Check(!IsDirt(world, 3, 3) && IsDirt(world, 3 + direction.X, 3 + direction.Y), "field direction did not move exactly one neighbouring cell");
        Check(DirtCount(world) == 1, "directional movement lost or duplicated a block");
        world.Tick(0.25f);
        const uint32_t edgeX = direction.X < 0 ? 0 : direction.X > 0 ? 6
                                                                     : 3;
        const uint32_t edgeY = direction.Y < 0 ? 0 : direction.Y > 0 ? 6
                                                                     : 3;
        Check(IsDirt(world, edgeX, edgeY) && DirtCount(world) == 1, "directional movement crossed the world boundary");
        for (const auto cell : world.ReadState().Cells)
            Check(cell.Force == direction, "movement transported or cleared the spatial field");
    }
    pixel::World still(3, 3);
    still.SetForceDirection(1, 1, {0, 0});
    still.PlaceDirt(1, 1);
    still.Tick(0.25f);
    Check(IsDirt(still, 1, 1), "zero force moved a block");
}

void SpatialFieldAndPublication() {
    pixel::World world(4, 4);
    world.SetForceDirection(1, 1, {1, 0});
    world.SetForceDirection(2, 1, {0, 1});
    world.SetForceDirection(2, 2, {-1, 0});
    world.SetForceDirection(1, 2, {0, 0});
    world.PlaceDirt(1, 1);
    Check(world.GetCell(1, 1).Force == pixel::ForceDirection{1, 0}, "placement erased the field");
    pixel::WorldState snapshot(4, 4);
    world.PublishState(snapshot);
    const std::vector<pixel::CellState> published(snapshot.Read().Cells.begin(), snapshot.Read().Cells.end());
    constexpr std::array<std::array<uint32_t, 2>, 4> path{{{2, 1}, {2, 2}, {1, 2}, {1, 2}}};
    for (const auto position : path) {
        world.Tick(1.f / 60);
        Check(IsDirt(world, position[0], position[1]) && DirtCount(world) == 1, "block failed to follow a changing spatial field");
        for (size_t i = 0; i < published.size(); ++i)
            Check(world.ReadState().Cells[i].Force == published[i].Force, "spatial field moved with material");
    }
    world.SetForceDirection(1, 2, {-1, -1});
    Check(std::equal(published.begin(), published.end(), snapshot.Read().Cells.begin()), "simulation or field edit mutated a published snapshot");
    world.PublishState(snapshot);
    Check(snapshot.Read().Cells[9].Force == pixel::ForceDirection{-1, -1} && snapshot.Read().Cells[9].Type == pixel::CellTypes::Dirt, "publication lost field or material changes");
    world.RemoveDirt(1, 2);
    Check(world.GetCell(1, 2).Force == pixel::ForceDirection{-1, -1}, "removal erased the field");
    world.PlaceDirt(1, 2);
    world.Tick(1.f / 60);
    Check(IsDirt(world, 0, 1), "replaced block did not use the retained field");
    world.GenerateTerrain(2);
    for (const auto cell : world.ReadState().Cells)
        Check(cell.Force == pixel::ForceDirection{0, 1}, "terrain reset retained an edited field");
}

void ChainsAndObstacles() {
    for (const auto direction : Directions) {
        pixel::World world(7, 7);
        world.FillForceDirection(direction);
        for (int distance = 0; distance < 3; ++distance)
            world.PlaceDirt(3 - distance * direction.X, 3 - distance * direction.Y);
        world.Tick(1.f / 60);
        for (int distance = -1; distance < 2; ++distance)
            Check(IsDirt(world, 3 - distance * direction.X, 3 - distance * direction.Y), "directional stack did not follow its leading block");
        Check(DirtCount(world) == 3, "moving a directional stack changed its block count");
    }
    pixel::World blocked(5, 3);
    blocked.FillForceDirection({1, 0});
    blocked.PlaceDirt(1, 1);
    blocked.PlaceDirt(2, 1);
    blocked.SetCell(3, 1, {{20}, {0, 0}});
    blocked.Tick(1.f / 60);
    Check(IsDirt(blocked, 1, 1) && IsDirt(blocked, 2, 1) && blocked.GetCell(3, 1).Type.Value == 20, "force overwrote an immovable obstacle");

    pixel::World bent(4, 4);
    bent.SetForceDirection(1, 1, {1, 0});
    bent.SetForceDirection(2, 1, {0, 1});
    bent.SetForceDirection(2, 2, {1, 0});
    bent.PlaceDirt(1, 1);
    bent.PlaceDirt(2, 1);
    bent.PlaceDirt(2, 2);
    bent.Tick(1.f / 60);
    Check(!IsDirt(bent, 1, 1) && IsDirt(bent, 2, 1) && IsDirt(bent, 2, 2) && IsDirt(bent, 3, 2), "bent movement chain failed");

    pixel::World longChain(1, 32768);
    longChain.Fill({pixel::CellTypes::Dirt});
    longChain.RemoveDirt(0, 32767);
    longChain.Tick(1.f / 60);
    Check(!IsDirt(longChain, 0, 0) && IsDirt(longChain, 0, 32767) && DirtCount(longChain) == 32767, "long movement chain failed");
}

void ContentionAndCycles() {
    pixel::World contest(5, 5);
    contest.FillForceDirection({0, 0});
    contest.SetForceDirection(2, 1, {0, 1});
    contest.SetForceDirection(1, 2, {1, 0});
    contest.SetForceDirection(3, 2, {-1, 0});
    contest.PlaceDirt(2, 1);
    contest.PlaceDirt(1, 2);
    contest.PlaceDirt(3, 2);
    contest.Tick(1.f / 60);
    Check(!IsDirt(contest, 2, 1) && IsDirt(contest, 2, 2) && IsDirt(contest, 1, 2) && IsDirt(contest, 3, 2), "destination contention did not preserve deterministic priority");
    Check(DirtCount(contest) == 3, "destination contention lost a block");
    pixel::World cycle(4, 4);
    cycle.SetCell(1, 1, {pixel::CellTypes::Dirt, {1, 0}});
    cycle.SetCell(2, 1, {pixel::CellTypes::Dirt, {0, 1}});
    cycle.SetCell(2, 2, {pixel::CellTypes::Dirt, {-1, 0}});
    cycle.SetCell(1, 2, {pixel::CellTypes::Dirt, {0, -1}});
    const std::vector<pixel::CellState> before(cycle.ReadState().Cells.begin(), cycle.ReadState().Cells.end());
    cycle.Tick(0.25f);
    Check(std::equal(before.begin(), before.end(), cycle.ReadState().Cells.begin()), "closed occupied cycle moved or lost material");
    cycle.Fill({});
    cycle.SetCell(1, 1, {pixel::CellTypes::Dirt, {1, 0}});
    cycle.SetCell(2, 1, {pixel::CellTypes::Dirt, {-1, 0}});
    cycle.Tick(1.f / 60);
    Check(IsDirt(cycle, 1, 1) && IsDirt(cycle, 2, 1) && DirtCount(cycle) == 2, "opposing blocks passed through each other");
}

void FieldValidationAndConservation() {
    pixel::World world(13, 11);
    auto reject = [](auto action) {
        try {
            action();
        } catch (const std::invalid_argument &) {
            return;
        }
        throw std::runtime_error("invalid field direction accepted");
    };
    reject([&] { world.SetForceDirection(1, 1, {2, 0}); });
    reject([&] { world.FillForceDirection({0, -2}); });
    reject([&] { world.SetCell(1, 1, {pixel::CellTypes::Dirt, {INT32_MAX, 0}}); });
    reject([&] { world.Fill({pixel::CellTypes::Dirt, {0, INT32_MIN}}); });
    for (const auto cell : world.ReadState().Cells)
        Check(cell == pixel::CellState{}, "rejected field update mutated the world");
    std::mt19937 random(20261004);
    for (uint32_t y = 0; y < 11; ++y)
        for (uint32_t x = 0; x < 13; ++x) {
            const uint32_t kind = random() % 5;
            world.SetCell(x, y, {{kind == 0 ? 20u : kind < 3 ? 10u
                                                             : 0u},
                                 Directions[random() % Directions.size()]});
        }
    const auto count = DirtCount(world);
    const std::vector<pixel::CellState> initial(world.ReadState().Cells.begin(), world.ReadState().Cells.end());
    pixel::WorldState snapshot(13, 11);
    for (int step = 0; step < 120; ++step) {
        world.Tick(1.f / 60);
        if (step % 7 == 0)
            world.PublishState(snapshot);
        Check(DirtCount(world) == count, "mixed field simulation lost or created material");
        for (size_t i = 0; i < initial.size(); ++i) {
            Check(world.ReadState().Cells[i].Force == initial[i].Force, "mixed field changed a spatial force");
            if (initial[i].Type.Value == 20)
                Check(world.ReadState().Cells[i].Type.Value == 20, "mixed field overwrote an obstacle");
        }
    }
}
void RandomUpperField() {
    pixel::World world(32, 1024), sameSeed(32, 1024);
    world.GenerateTerrain(512);
    sameSeed.GenerateTerrain(512);
    world.RandomizeForceDirections(1024 - 600, 20261004);
    sameSeed.RandomizeForceDirections(1024 - 600, 20261004);
    Check(std::equal(world.ReadState().Cells.begin(), world.ReadState().Cells.end(), sameSeed.ReadState().Cells.begin()), "seeded field is not reproducible");
    std::array<bool, 8> seen{};
    size_t stableAirCells = 0;
    for (uint32_t y = 0; y < 1024; ++y)
        for (uint32_t x = 0; x < 32; ++x) {
            const auto cell = world.GetCell(x, y);
            Check(cell.Type == (y < 512 ? pixel::CellTypes::Empty : pixel::CellTypes::Dirt), "randomizing the field changed terrain");
            const uint32_t height = 1023 - y;
            if (height >= 600) {
                const auto direction = std::find(Directions.begin(), Directions.end(), cell.Force);
                Check(direction != Directions.end(), "random field contains an invalid or zero direction");
                seen[size_t(direction - Directions.begin())] = true;
            } else {
                Check(cell.Force == pixel::ForceDirection{0, 1}, "random field changed the lower gravity region");
                if (height >= 512) {
                    Check(cell.Type == pixel::CellTypes::Empty, "stable sky band is not empty");
                    ++stableAirCells;
                }
            }
        }
    Check(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }), "random field does not cover all directions");
    Check(stableAirCells == 88 * 32, "512-600 height range does not contain 88 stable sky rows");
    world.RandomizeForceDirections(0, 1);
    Check(std::equal(world.ReadState().Cells.begin(), world.ReadState().Cells.end(), sameSeed.ReadState().Cells.begin()), "empty random region changed the field");
    bool rejected = false;
    try {
        world.RandomizeForceDirections(1025, 1);
    } catch (const std::out_of_range &) {
        rejected = true;
    }
    Check(rejected && std::equal(world.ReadState().Cells.begin(), world.ReadState().Cells.end(), sameSeed.ReadState().Cells.begin()), "invalid random region changed the field");
    world.Tick(0.25f);
    Check(std::equal(world.ReadState().Cells.begin(), world.ReadState().Cells.end(), sameSeed.ReadState().Cells.begin()), "initial terrain moved under the default field");
    world.PlaceDirt(16, 424);
    for (int step = 0; step < 88; ++step)
        world.Tick(1.f / 60);
    Check(!IsDirt(world, 16, 424) && IsDirt(world, 16, 511), "block did not fall through the stable 512-600 sky band");
    world.RemoveDirt(16, 511);
    Check(std::equal(world.ReadState().Cells.begin(), world.ReadState().Cells.end(), sameSeed.ReadState().Cells.begin()), "stable-band movement disturbed other cells or transported the field");
}
} // namespace

void RunPixelFieldTests() {
    MovementDirections();
    SpatialFieldAndPublication();
    ChainsAndObstacles();
    ContentionAndCycles();
    FieldValidationAndConservation();
    RandomUpperField();
}
