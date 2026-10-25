#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>

namespace pixel::storage {
#ifndef PIXEL_CHUNK_EDGE
#define PIXEL_CHUNK_EDGE 1024
#endif
inline constexpr uint32_t ChunkEdge = PIXEL_CHUNK_EDGE;
inline constexpr uint32_t RegionEdge = 64;
inline constexpr size_t ChunkAlignment = 64 * 1024;
inline constexpr size_t ChunkCells = size_t(ChunkEdge) * ChunkEdge;
inline constexpr size_t ChunkReservedBytes = (ChunkCells * 14 + ChunkAlignment - 1) / ChunkAlignment * ChunkAlignment;
inline constexpr uint32_t RegionsPerAxis = ChunkEdge / RegionEdge;
inline constexpr size_t RegionsPerChunk = size_t(RegionsPerAxis) * RegionsPerAxis;
inline constexpr uint32_t DefaultWindowEdge = 1024;
inline constexpr uint64_t DefaultBudget = 512ull * 1024 * 1024;
static_assert(std::has_single_bit(ChunkEdge) && std::has_single_bit(RegionEdge));
static_assert(ChunkEdge >= RegionEdge && ChunkEdge % RegionEdge == 0);
static_assert(std::has_single_bit(ChunkAlignment));
} // namespace pixel::storage
