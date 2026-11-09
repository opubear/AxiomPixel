#include "DisplaySettings.h"
#include "Runtime/Gameplay/Camera/PixelScale.h"
#include "Runtime/Gameplay/UI/UIScale.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace pixel {
DisplaySettings DisplaySettings::Parse(std::string_view text) {
    const auto json = nlohmann::json::parse(text);
    if (!json.is_object())
        throw std::invalid_argument("game config must be an object");
    DisplaySettings settings;
    const auto read = [&](const char *name, float &value, auto valid) {
        if (!json.contains(name)) return;
        const auto &entry = json.at(name);
        if (!entry.is_number() || !valid(entry.get<float>()))
            throw std::invalid_argument(std::string(name) + " is outside the supported display scale range");
        value = entry.get<float>();
    };
    read("SceneScale", settings.SceneScale, IsValidSceneScale);
    read("UIScale", settings.UIScale, ui::IsValidUIScale);
    return settings;
}
} // namespace pixel
