#pragma once
#include "GameAPI/WorldWillState.h"
#include <string>
#include <string_view>

namespace pixel {
std::string EncodeWorldWillState(const api::WorldWillMemory &state);
api::WorldWillMemory DecodeWorldWillState(std::string_view payload);
} // namespace pixel
