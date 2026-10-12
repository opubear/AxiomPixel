#include "World.h"
#include "FieldResponse.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>

namespace pixel {
namespace {
void ValidateForceDirection(ForceDirection direction) {
    if (!direction.IsValid())
        throw std::invalid_argument("force direction must select a neighbouring cell or zero");
}
} // namespace
World::World(uint32_t width, uint32_t height) : State(width, height), BaseForces(width, height) {
    BaseForces.Fill(FieldResponse::FromDirection({0, 1}));
}
size_t World::CellIndex(uint32_t x, uint32_t y) const {
    if (x >= State.Width || y >= State.Height)
        throw std::out_of_range("cell coordinates are outside the world");
    return size_t(y) * State.Width + x;
}
CellState World::GetCell(uint32_t x, uint32_t y) const { return State.Cells[CellIndex(x, y)]; }
void World::SetCell(uint32_t x, uint32_t y, CellState cell) {
    ValidateForceDirection(cell.Force);
    State.Cells[CellIndex(x, y)] = cell;
    BaseForces.Set(x, y, FieldResponse::FromDirection(cell.Force));
}
void World::Fill(CellState cell) {
    ValidateForceDirection(cell.Force);
    std::fill(State.Cells.begin(), State.Cells.end(), cell);
    BaseForces.Fill(FieldResponse::FromDirection(cell.Force));
}
void World::SetForceDirection(uint32_t x, uint32_t y, ForceDirection direction) {
    auto cell = GetCell(x, y);
    cell.Force = direction;
    SetCell(x, y, cell);
}
void World::FillForceDirection(ForceDirection direction) {
    ValidateForceDirection(direction);
    for (auto &cell : State.Cells)
        cell.Force = direction;
    BaseForces.Fill(FieldResponse::FromDirection(direction));
}
void World::RandomizeForceDirections(uint32_t endRow, uint32_t seed) {
    if (endRow > State.Height)
        throw std::out_of_range("random field boundary is outside the world");
    constexpr std::array<ForceDirection, 8> directions{{{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
    std::mt19937 random(seed);
    std::uniform_int_distribution<size_t> choose(0, directions.size() - 1);
    for (size_t index = 0; index < size_t(endRow) * State.Width; ++index) {
        const auto direction = directions[choose(random)];
        State.Cells[index].Force = direction;
        BaseForces.Set(uint32_t(index % State.Width), uint32_t(index / State.Width), FieldResponse::FromDirection(direction));
    }
}
void World::ConfigureWind(uint32_t endRow, const WindSettings &settings) {
    if (!endRow || endRow > State.Height)
        throw std::out_of_range("wind boundary is outside the world");
    Environment.GetWindSimulation().ConfigureCoarse(State.Width, endRow, settings);
}
void World::ConfigurePixelWind(uint32_t endRow, const PixelWindSettings &settings) {
    if (!endRow || endRow > State.Height)
        throw std::out_of_range("wind boundary is outside the world");
    Environment.GetWindSimulation().ConfigureReference(ReadState(), endRow, settings);
}
void World::ConfigureWindStrategy(uint32_t endRow, const WindStrategySettings &settings) {
    Environment.GetWindSimulation().Configure(ReadState(), endRow, settings);
}
ForceVector World::GetForce(uint32_t x, uint32_t y) const {
    return Environment.SampleForce(BaseForces.Get(x, y), x, y);
}
WindVelocity World::GetFieldVelocity(uint32_t x, uint32_t y) const {
    return FieldResponse::MovementVelocity(GetForce(x, y));
}
void World::MoveMaterial(size_t source, size_t destination) {
    State.Cells[destination].Type = State.Cells[source].Type;
    State.Cells[source].Type = CellTypes::Empty;
}
void World::GenerateTerrain(uint32_t surfaceRow) {
    if (surfaceRow > State.Height)
        throw std::out_of_range("terrain surface is outside the world");
    Fill({});
    std::fill(State.Cells.begin() + size_t(surfaceRow) * State.Width, State.Cells.end(), CellState{CellTypes::Dirt});
    Environment = {};
    Player.reset();
    PlayerHorizontal = 0;
    PlayerJump = false;
}
bool World::PlaceDirt(uint32_t x, uint32_t y) {
    auto &cell = State.Cells[CellIndex(x, y)];
    if ((Player && Player->Occupies(int(x), int(y))) || cell.Type != CellTypes::Empty)
        return false;
    cell.Type = CellTypes::Dirt;
    return true;
}
bool World::RemoveDirt(uint32_t x, uint32_t y) {
    auto &cell = State.Cells[CellIndex(x, y)];
    if (cell.Type != CellTypes::Dirt)
        return false;
    cell.Type = CellTypes::Empty;
    return true;
}
void World::PublishState(WorldState &destination) {
    if (&destination == &State || destination.Width != State.Width || destination.Height != State.Height)
        throw std::invalid_argument("world publication requires distinct, equal-sized states");
    State.Cells.swap(destination.Cells);
    std::copy(destination.Cells.begin(), destination.Cells.end(), State.Cells.begin());
}
void World::SpawnPlayer(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
        x + Character::Width > State.Width || y + Character::Height > State.Height)
        throw std::invalid_argument("player bounds must fit inside the world");
    Player = Character{};
    Player->X = x;
    Player->Y = y;
    PlayerHorizontal = 0;
    PlayerJump = false;
}
void World::SetPlayerInput(int horizontal, bool jump) {
    PlayerHorizontal = std::clamp(horizontal, -1, 1);
    PlayerJump = jump;
}
void World::Tick(float duration) {
    Environment.Tick(duration, *this);
    if (Player)
        Player->Tick(duration, ReadState(), PlayerHorizontal, PlayerJump);
    PlayerJump = false;
}
} // namespace pixel
