#pragma once
#include "ViewService.h"

namespace pixel::llm {
QueryPage ProjectQuery(const api::ObservationSnapshot &snapshot, const ViewDescriptor &view,
                       QuerySpec query, uint32_t offset);
bool ValidRect(api::Rect rect);
bool Contains(api::Rect outer, api::Rect inner);
} // namespace pixel::llm
