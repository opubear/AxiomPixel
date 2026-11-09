#pragma once

#include "WorldStorageConfig.h"
#include "WorldUnits.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace pixel {
struct CellCoord {
    int64_t X = 0, Y = 0;
    bool operator==(const CellCoord &) const = default;
};

struct CellCoordLess {
    bool operator()(CellCoord a, CellCoord b) const {
        return a.Y < b.Y || (a.Y == b.Y && a.X < b.X);
    }
};

inline int64_t FloorDivide(int64_t value, int64_t divisor) {
    if (divisor <= 0)
        throw std::invalid_argument("floor division requires a positive divisor");
    const auto quotient = value / divisor;
    return quotient - (value % divisor < 0 ? 1 : 0);
}

inline CellCoord ChunkOf(CellCoord cell) {
    return {FloorDivide(cell.X, storage::ChunkEdge), FloorDivide(cell.Y, storage::ChunkEdge)};
}

inline CellCoord RegionOf(CellCoord cell) {
    return {FloorDivide(cell.X, storage::RegionEdge), FloorDivide(cell.Y, storage::RegionEdge)};
}

struct WorldRect {
    int64_t X = 0, Y = 0;
    uint32_t Width = 0, Height = 0;

    bool Empty() const { return Width == 0 || Height == 0; }
    bool Contains(CellCoord p) const {
        return p.X >= X && p.Y >= Y && uint64_t(p.X - X) < Width && uint64_t(p.Y - Y) < Height;
    }
    bool Intersects(const WorldRect &other) const {
        return !Empty() && !other.Empty() && X < other.X + other.Width && other.X < X + Width && Y < other.Y + other.Height && other.Y < Y + Height;
    }
    bool operator==(const WorldRect &) const = default;
};

// Integer chunk addresses plus float centimetres within the chunk. Never merge a
// distant chunk address into a float before subtracting the local reference.
struct WorldPosition {
    CellCoord Chunk;
    std::array<float, 2> Local{};
    static constexpr float ChunkCentimeters = storage::ChunkEdge * CentimetersPerCell;

    void Normalize() {
        for (size_t axis = 0; axis < 2; ++axis) {
            if (!std::isfinite(Local[axis]) || std::abs(Local[axis]) > float(int64_t{1} << 50))
                throw std::invalid_argument("invalid continuous world position");
            const auto shift = FloorDivide(int64_t(std::floor(Local[axis])), int64_t(ChunkCentimeters));
            auto &chunk = axis == 0 ? Chunk.X : Chunk.Y;
            constexpr int64_t limit = int64_t{1} << 49;
            if (chunk < -limit || chunk > limit || shift < -limit - chunk || shift > limit - chunk)
                throw std::out_of_range("world position exceeds supported coordinate range");
            chunk += shift;
            Local[axis] = std::fmod(Local[axis], ChunkCentimeters);
            if (Local[axis] < 0) {
                Local[axis] += ChunkCentimeters;
                // A tiny negative offset must stay on the preceding side of a boundary.
                if (Local[axis] >= ChunkCentimeters)
                    Local[axis] = std::nextafter(ChunkCentimeters, 0.f);
            }
        }
    }
    CellCoord Cell() const {
        return {Chunk.X * storage::ChunkEdge + int64_t(std::floor(CentimetersToCells(Local[0]))),
                Chunk.Y * storage::ChunkEdge + int64_t(std::floor(CentimetersToCells(Local[1])))};
    }
    std::array<float, 2> RelativeTo(CellCoord origin) const {
        return {CellsToCentimeters(float(Chunk.X * storage::ChunkEdge - origin.X)) + Local[0],
                CellsToCentimeters(float(Chunk.Y * storage::ChunkEdge - origin.Y)) + Local[1]};
    }
    static WorldPosition FromAbsolute(float x, float y) {
        WorldPosition result{{}, {x, y}};
        result.Normalize();
        return result;
    }

    // Explicit grid/protocol adapters. Double here preserves legacy cell-valued
    // serialization and solver arithmetic; the stored game position is float cm.
    std::array<double, 2> RelativeCellsTo(CellCoord origin) const {
        return {double(Chunk.X * storage::ChunkEdge - origin.X) + double(Local[0]) / CentimetersPerCell,
                double(Chunk.Y * storage::ChunkEdge - origin.Y) + double(Local[1]) / CentimetersPerCell};
    }
    static WorldPosition FromChunkCells(CellCoord chunk, std::array<double, 2> localCells) {
        WorldPosition result{chunk, {float(localCells[0] * CentimetersPerCell), float(localCells[1] * CentimetersPerCell)}};
        result.Normalize();
        return result;
    }
    static WorldPosition FromCell(CellCoord cell, std::array<double, 2> offsetCells = {}) {
        const auto chunk = ChunkOf(cell);
        return FromChunkCells(chunk, {double(cell.X - chunk.X * storage::ChunkEdge) + offsetCells[0],
                                     double(cell.Y - chunk.Y * storage::ChunkEdge) + offsetCells[1]});
    }
    static WorldPosition FromCells(double x, double y) {
        if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) >= double(int64_t{1} << 59)
            || std::abs(y) >= double(int64_t{1} << 59))
            throw std::invalid_argument("invalid cell position");
        const CellCoord cell{int64_t(std::floor(x)), int64_t(std::floor(y))};
        return FromCell(cell, {x - std::floor(x), y - std::floor(y)});
    }
};
} // namespace pixel
