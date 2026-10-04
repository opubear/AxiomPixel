#include "WorldState.h"
#include <stdexcept>

namespace pixel {
namespace {
size_t CheckedCellCount(uint32_t width, uint32_t height) {
    const uint64_t count = uint64_t(width) * height;
    if (!width || !height || count > std::vector<CellState>().max_size())
        throw std::invalid_argument("world dimensions must fit a nonempty cell array");
    return size_t(count);
}
} // namespace
WorldState::WorldState(uint32_t width, uint32_t height)
    : Width(width), Height(height), Cells(CheckedCellCount(width, height)) {}
} // namespace pixel
