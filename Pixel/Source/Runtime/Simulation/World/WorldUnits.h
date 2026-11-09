#pragma once

namespace pixel {
// Continuous game distances are float centimetres; terrain addresses remain cells.
inline constexpr float CentimetersPerCell = 50.f;
constexpr float CellsToCentimeters(float cells) { return cells * CentimetersPerCell; }
constexpr float CentimetersToCells(float centimeters) { return centimeters / CentimetersPerCell; }
} // namespace pixel
