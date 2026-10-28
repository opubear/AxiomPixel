#pragma once
#include "InventoryUI.h"

namespace pixel::ui {
// Native pixel primitives and a tiny bitmap font; no windowing or GPU dependency.
void PaintInventory(const InventoryView &view, std::vector<std::byte> &pixels);
}
