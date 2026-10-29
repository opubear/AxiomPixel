#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include "Runtime/System/Memory/ChunkedGrid.h"

namespace pixel::storage {
#ifndef PIXEL_CHUNK_EDGE
#define PIXEL_CHUNK_EDGE 1024
#endif
inline constexpr uint32_t ChunkEdge = PIXEL_CHUNK_EDGE;
inline constexpr uint32_t RegionEdge = memory::SpatialTileEdge;
inline constexpr size_t ChunkCells = size_t(ChunkEdge) * ChunkEdge;
inline constexpr size_t ChunkPayloadBytes = ChunkCells * 14;
// Column allocations are separate and page-rounded. The control object is
// accounted by WorldChunk, rather than pretending it is the pixel payload.
inline size_t ChunkReservedBytes() {
    // Runtime page size must not participate in cross-TU global initialization:
    // a stream reservation could otherwise observe a zero-initialized value.
    static const size_t bytes = memory::ChunkedGrid<uint32_t>::AllocationBytes(ChunkEdge, ChunkEdge)
        + memory::ChunkedGrid<uint16_t>::AllocationBytes(ChunkEdge, ChunkEdge)
        + memory::ChunkedGrid<uint64_t>::AllocationBytes(ChunkEdge, ChunkEdge);
    return bytes;
}
inline constexpr uint32_t RegionsPerAxis = ChunkEdge / RegionEdge;
inline constexpr size_t RegionsPerChunk = size_t(RegionsPerAxis) * RegionsPerAxis;
inline constexpr uint32_t DefaultWindowEdge = 1024;
inline constexpr uint64_t DefaultBudget = 512ull * 1024 * 1024;
static_assert(std::has_single_bit(ChunkEdge) && std::has_single_bit(RegionEdge));
static_assert(ChunkEdge >= RegionEdge && ChunkEdge % RegionEdge == 0);
} // namespace pixel::storage
