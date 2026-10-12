#pragma once

#include <cstdint>
#include <stdexcept>

namespace pixel {
// Gameplay identities, independent of texture IDs. Keep existing numeric IDs.
enum class CellTypeId : uint32_t {
    Air = 0,
    Dirt = 10,
};

// Compatibility names for existing world operations; empty space is air.
namespace CellTypes {
inline constexpr CellTypeId Air = CellTypeId::Air;
inline constexpr CellTypeId Empty = CellTypeId::Air;
inline constexpr CellTypeId Dirt = CellTypeId::Dirt;
} // namespace CellTypes

struct CellInfo {
    float Mass;
};

// Unsupported types have no definition. Each specialization owns one immutable
// instance shared across translation units, never copied into world cells.
template <CellTypeId Type>
struct CellDefinition;

template <>
struct CellDefinition<CellTypeId::Air> {
    inline static constexpr CellInfo Info{0.f};
};

template <>
struct CellDefinition<CellTypeId::Dirt> {
    inline static constexpr CellInfo Info{1.f};
};

constexpr const CellInfo &GetCellInfo(CellTypeId type) {
    switch (type) {
    case CellTypeId::Air:
        return CellDefinition<CellTypeId::Air>::Info;
    case CellTypeId::Dirt:
        return CellDefinition<CellTypeId::Dirt>::Info;
    }
    throw std::invalid_argument("cell type has no static information");
}
} // namespace pixel
