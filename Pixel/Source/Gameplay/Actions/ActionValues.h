#pragma once
#include "GameAPI/Actions.h"
#include "World/WorldCoordinates.h"

namespace pixel {
inline api::Position ActionPosition(WorldPosition position) {
    position.Normalize();
    const auto cell = position.Cell();
    return {{cell.X, cell.Y}, {position.Local[0] - std::floor(position.Local[0]),
                              position.Local[1] - std::floor(position.Local[1])}};
}
// Formula stays in the game adapter, outside the standard-library GameAPI values.
struct ApplyProgramResult {
    api::Reason Rejection = api::Reason::None;
    std::optional<api::ProgramRef> Program;
    std::string Message;
    explicit operator bool() const { return Program.has_value(); }
};
} // namespace pixel
