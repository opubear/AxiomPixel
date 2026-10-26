#pragma once

#include <cstdint>
#include <stdexcept>

namespace pixel {
// Gameplay identities, independent of texture IDs. Keep existing numeric IDs.
enum class CellTypeId : uint32_t {
    Air = 0,
    Dirt = 10,
    GroundSoil = 20,
    GrassSoil = 21,
    Stone = 30,
    CoalOre = 40,
    CopperOre = 41,
    IronOre = 42,
    TungstenOre = 43,
    GoldOre = 44,
    Water = 50,
    Steam = 51,
    Fire = 52,
};

// Compatibility names for existing world operations; empty space is air.
namespace CellTypes {
inline constexpr CellTypeId Air = CellTypeId::Air;
inline constexpr CellTypeId Empty = CellTypeId::Air;
inline constexpr CellTypeId Dirt = CellTypeId::Dirt;
} // namespace CellTypes

struct CellInfo {
    float Mass;
    bool BlocksCharacter = true;
    bool BlocksWind = true;
    bool Mineable = true;
};

// Unsupported types have no definition. Each specialization owns one immutable
// instance shared across translation units, never copied into world cells.
template <CellTypeId Type>
struct CellDefinition;

template <>
struct CellDefinition<CellTypeId::Air> {
    inline static constexpr CellInfo Info{0.f, false, false, false};
};

template <>
struct CellDefinition<CellTypeId::Dirt> {
    inline static constexpr CellInfo Info{1.f};
};

template <> struct CellDefinition<CellTypeId::GroundSoil> { inline static constexpr CellInfo Info{1.f}; };
template <> struct CellDefinition<CellTypeId::GrassSoil> { inline static constexpr CellInfo Info{1.f}; };
template <> struct CellDefinition<CellTypeId::Stone> { inline static constexpr CellInfo Info{2.f}; };
template <> struct CellDefinition<CellTypeId::CoalOre> { inline static constexpr CellInfo Info{2.f}; };
template <> struct CellDefinition<CellTypeId::CopperOre> { inline static constexpr CellInfo Info{2.f}; };
template <> struct CellDefinition<CellTypeId::IronOre> { inline static constexpr CellInfo Info{2.f}; };
template <> struct CellDefinition<CellTypeId::TungstenOre> { inline static constexpr CellInfo Info{2.f}; };
template <> struct CellDefinition<CellTypeId::GoldOre> { inline static constexpr CellInfo Info{2.f}; };
template <> struct CellDefinition<CellTypeId::Water> { inline static constexpr CellInfo Info{1.f, false, true, false}; };
template <> struct CellDefinition<CellTypeId::Steam> { inline static constexpr CellInfo Info{0.f, false, false, false}; };
template <> struct CellDefinition<CellTypeId::Fire> { inline static constexpr CellInfo Info{0.f, false, false, false}; };

constexpr const CellInfo &GetCellInfo(CellTypeId type) {
    switch (type) {
    case CellTypeId::Air:
        return CellDefinition<CellTypeId::Air>::Info;
    case CellTypeId::Dirt:
        return CellDefinition<CellTypeId::Dirt>::Info;
    case CellTypeId::GroundSoil: return CellDefinition<CellTypeId::GroundSoil>::Info;
    case CellTypeId::GrassSoil: return CellDefinition<CellTypeId::GrassSoil>::Info;
    case CellTypeId::Stone: return CellDefinition<CellTypeId::Stone>::Info;
    case CellTypeId::CoalOre: return CellDefinition<CellTypeId::CoalOre>::Info;
    case CellTypeId::CopperOre: return CellDefinition<CellTypeId::CopperOre>::Info;
    case CellTypeId::IronOre: return CellDefinition<CellTypeId::IronOre>::Info;
    case CellTypeId::TungstenOre: return CellDefinition<CellTypeId::TungstenOre>::Info;
    case CellTypeId::GoldOre: return CellDefinition<CellTypeId::GoldOre>::Info;
    case CellTypeId::Water: return CellDefinition<CellTypeId::Water>::Info;
    case CellTypeId::Steam: return CellDefinition<CellTypeId::Steam>::Info;
    case CellTypeId::Fire: return CellDefinition<CellTypeId::Fire>::Info;
    }
    throw std::invalid_argument("cell type has no static information");
}

constexpr bool IsKnownCellType(CellTypeId type) {
    switch (type) {
    case CellTypeId::Air: case CellTypeId::Dirt: case CellTypeId::GroundSoil:
    case CellTypeId::GrassSoil: case CellTypeId::Stone: case CellTypeId::CoalOre:
    case CellTypeId::CopperOre: case CellTypeId::IronOre: case CellTypeId::TungstenOre:
    case CellTypeId::GoldOre: case CellTypeId::Water:
    case CellTypeId::Steam: case CellTypeId::Fire: return true;
    }
    return false;
}

// Missing numerical geometry uses an unregistered sentinel. Keep it closed.
constexpr bool BlocksCharacter(CellTypeId type) { return !IsKnownCellType(type) || GetCellInfo(type).BlocksCharacter; }
constexpr bool BlocksWind(CellTypeId type) { return !IsKnownCellType(type) || GetCellInfo(type).BlocksWind; }
constexpr bool IsMineable(CellTypeId type) { return IsKnownCellType(type) && GetCellInfo(type).Mineable; }
} // namespace pixel
