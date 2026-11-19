#pragma once

#include "Runtime/Gameplay/Items/PlayerProfile.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace pixel::testing {
inline items::PlayerProfile ReadPlayerProfile(std::string_view name) {
    std::ifstream file(std::filesystem::path(PIXEL_TEST_CONTENT) / "PlayerProfiles.json");
    if (!file) throw std::runtime_error("missing player profiles fixture content");
    const std::string text{std::istreambuf_iterator<char>(file), {}};
    return items::PlayerProfile::Parse(text, name);
}
} // namespace pixel::testing
