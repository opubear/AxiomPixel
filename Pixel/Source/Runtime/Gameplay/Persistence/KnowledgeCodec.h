#pragma once
#include "Runtime/Gameplay/Knowledge/KnowledgeTypes.h"
#include <string>
#include <string_view>

namespace pixel {
std::string EncodeKnowledgeState(const knowledge::SaveData &state);
knowledge::SaveData DecodeKnowledgeState(std::string_view payload);
} // namespace pixel
