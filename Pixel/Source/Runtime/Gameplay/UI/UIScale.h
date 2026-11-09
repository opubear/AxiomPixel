#pragma once

namespace pixel::ui {
inline constexpr float MinimumUIScale = .25f;
inline constexpr float MaximumUIScale = 4.f;

constexpr bool IsValidUIScale(float scale) {
    return scale >= MinimumUIScale && scale <= MaximumUIScale;
}
} // namespace pixel::ui
