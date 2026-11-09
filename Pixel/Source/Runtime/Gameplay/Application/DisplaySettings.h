#pragma once
#include <string_view>

namespace pixel {
struct DisplaySettings {
    float SceneScale = 1;
    float UIScale = 1;

    // Pixel-owned fields in GameConfig.json; absent fields retain defaults.
    static DisplaySettings Parse(std::string_view text);
};
} // namespace pixel
