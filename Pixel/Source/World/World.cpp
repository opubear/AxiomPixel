#include "World.h"
#include <algorithm>
#include <array>
#include <random>
#include <stdexcept>

namespace pixel {
namespace {
void ValidateForceDirection(ForceDirection direction) {
    if (!direction.IsValid())
        throw std::invalid_argument("force direction must select a neighbouring cell or zero");
}
} // namespace
size_t World::CellIndex(uint32_t x, uint32_t y) const {
    if (x >= State.Width || y >= State.Height)
        throw std::out_of_range("cell coordinates are outside the world");
    return size_t(y) * State.Width + x;
}
CellState World::GetCell(uint32_t x, uint32_t y) const { return State.Cells[CellIndex(x, y)]; }
void World::SetCell(uint32_t x, uint32_t y, CellState cell) {
    ValidateForceDirection(cell.Force);
    State.Cells[CellIndex(x, y)] = cell;
}
void World::Fill(CellState cell) {
    ValidateForceDirection(cell.Force);
    std::fill(State.Cells.begin(), State.Cells.end(), cell);
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
}
void World::RandomizeForceDirections(uint32_t endRow, uint32_t seed) {
    if (endRow > State.Height)
        throw std::out_of_range("random field boundary is outside the world");
    constexpr std::array<ForceDirection, 8> directions{{{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}};
    std::mt19937 random(seed);
    std::uniform_int_distribution<size_t> choose(0, directions.size() - 1);
    for (size_t index = 0; index < size_t(endRow) * State.Width; ++index)
        State.Cells[index].Force = directions[choose(random)];
}
void World::ConfigureWind(uint32_t endRow, const WindSettings &settings) {
    if (!endRow || endRow > State.Height)
        throw std::out_of_range("wind boundary is outside the world");
    Environment.GetWind().Reset(State.Width, endRow, settings);
}
WindVelocity World::GetFieldVelocity(uint32_t x, uint32_t y) const {
    return Environment.SampleFieldVelocity(GetCell(x, y).Force, float(x) + 0.5f, float(y) + 0.5f);
}
void World::GenerateTerrain(uint32_t surfaceRow) {
    if (surfaceRow > State.Height)
        throw std::out_of_range("terrain surface is outside the world");
    Fill({});
    std::fill(State.Cells.begin() + size_t(surfaceRow) * State.Width, State.Cells.end(), CellState{CellTypes::Dirt});
    Environment = {};
}
bool World::PlaceDirt(uint32_t x, uint32_t y) {
    auto &cell = State.Cells[CellIndex(x, y)];
    if (cell.Type != CellTypes::Empty)
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
void World::Tick(float duration) {
    TickCharacters(duration);
    Environment.Tick(duration, *this);
}
void World::TickCharacters(float) {
    // Player and NPC state/behaviors belong here when characters are introduced.
    // The initial world has no characters.
}
} // namespace pixel
