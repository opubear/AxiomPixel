#pragma once

#include "WorldStorageConfig.h"
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

// Continuous positions keep fractional precision independently of distance from origin.
struct WorldPosition {
    CellCoord Chunk;
    std::array<double, 2> Local{};

    void Normalize() {
        for (size_t axis = 0; axis < 2; ++axis) {
            if (!std::isfinite(Local[axis]) || std::abs(Local[axis]) > double(int64_t{1} << 50))
                throw std::invalid_argument("invalid continuous world position");
            const auto shift = int64_t(std::floor(Local[axis] / storage::ChunkEdge));
            auto &chunk = axis == 0 ? Chunk.X : Chunk.Y;
            if (std::abs(double(chunk)) + std::abs(double(shift)) > double(int64_t{1} << 49))
                throw std::out_of_range("world position exceeds supported coordinate range");
            chunk += shift;
            Local[axis] -= double(shift) * storage::ChunkEdge;
        }
    }
    CellCoord Cell() const {
        return {Chunk.X * storage::ChunkEdge + int64_t(std::floor(Local[0])),
                Chunk.Y * storage::ChunkEdge + int64_t(std::floor(Local[1]))};
    }
    std::array<double, 2> RelativeTo(CellCoord origin) const {
        return {double(Chunk.X * storage::ChunkEdge - origin.X) + Local[0],
                double(Chunk.Y * storage::ChunkEdge - origin.Y) + Local[1]};
    }
    static WorldPosition FromAbsolute(double x, double y) {
        WorldPosition result{{}, {x, y}};
        result.Normalize();
        return result;
    }
};
} // namespace pixel
