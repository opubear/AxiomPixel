#pragma once

#include "GridGeometry.h"
#include "PageBuffer.h"
#include <cassert>
#include <compare>
#include <iterator>
#include <numeric>

namespace pixel::memory {
// Reusable only among fields with the same extent and tile size. The offset is
// opaque to callers; differing scalar sizes do not change the sample ordering.
class GridAddress {
    template<class, uint32_t> friend class ChunkedGrid;
    explicit GridAddress(size_t offset) : Offset(offset) {}
    size_t Offset;
};
// Explicit host layout for a tiled GPU storage buffer. Never upload this C++
// descriptor as a shader struct; serialize its integer fields for the backend.
struct GridLayout {
    uint32_t Width = 0, Height = 0, TileWidth = 0, TileHeight = 0, TilesX = 0, TilesY = 0;
    size_t ElementBytes = 0, ElementAlignment = 0, RowPitchBytes = 0, TilePitchBytes = 0, StorageBytes = 0;

    size_t ByteOffset(uint32_t x, uint32_t y) const {
        assert(x < Width && y < Height);
        return (size_t(y / TileHeight) * TilesX + x / TileWidth) * TilePitchBytes
            + (y % TileHeight) * RowPitchBytes + (x % TileWidth) * ElementBytes;
    }
};

// Dense field with tiled physical storage and stable logical row-major indices.
// TileEdge counts SAMPLES, not world pixels. No implicit contiguous range/data()
// conversion: consumers must choose a tile view or explicit row-major transfer.
template<class T, uint32_t TileEdge = SpatialTileEdge>
class ChunkedGrid {
    static_assert(std::has_single_bit(TileEdge));
    static_assert(std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>);
  public:
    using value_type = T;
    static constexpr uint32_t TileSize = TileEdge;
    ChunkedGrid() = default;
    ChunkedGrid(uint32_t width, uint32_t height, T value = {}, GridGeometry geometry = {}) { Reset(width, height, value, geometry); }
    ChunkedGrid(const ChunkedGrid &) = default;
    ChunkedGrid(ChunkedGrid &&other) noexcept { swap(other); }
    ChunkedGrid &operator=(const ChunkedGrid &other) {
        if (Width() == other.Width() && Height() == other.Height()) {
            Storage = other.Storage;
            Geometry = other.Geometry;
        } else {
            ChunkedGrid next(other);
            swap(next);
        }
        return *this;
    }
    ChunkedGrid &operator=(ChunkedGrid &&other) noexcept { ChunkedGrid next(std::move(other)); swap(next); return *this; }

    static GridLayout MakeLayout(uint32_t width, uint32_t height) {
        if (!width || !height) return {};
        GridLayout result{width, height, std::min(width, TileEdge), std::min(height, TileEdge),
                          (width - 1) / TileEdge + 1, (height - 1) / TileEdge + 1, sizeof(T), alignof(T)};
        // A common sample ordering allows solvers to calculate a neighbour's
        // address once for all columns. Tile starts are 128-byte aligned; rows
        // and elements retain T's alignment without padding individual scalars.
        result.RowPitchBytes = CheckedProduct(result.TileWidth, sizeof(T));
        result.TilePitchBytes = CheckedProduct(AlignBytes(CheckedProduct(result.TileWidth, result.TileHeight), TileAlignment), sizeof(T));
        result.StorageBytes = CheckedProduct(CheckedProduct(result.TilesX, result.TilesY), result.TilePitchBytes);
        return result;
    }
    static size_t AllocationBytes(uint32_t width, uint32_t height) {
        return PageBuffer<T>::AllocationBytes(MakeLayout(width, height).StorageBytes / sizeof(T));
    }
    void Reset(uint32_t width, uint32_t height, T value = {}, GridGeometry geometry = {}) {
        geometry.Validate();
        const auto layout = MakeLayout(width, height);
        if (layout.Width == Layout.Width && layout.Height == Layout.Height) {
            Geometry = geometry;
            Fill(value);
            return;
        }
        ChunkedGrid next;
        next.Layout = layout;
        next.RowStride = layout.RowPitchBytes / sizeof(T);
        next.TileStride = layout.TilePitchBytes / sizeof(T);
        next.WidthShift = std::has_single_bit(width) ? int(std::countr_zero(width)) : -1;
        next.Linear = layout.TilesX == 1 && next.RowStride == width
            && (layout.TilesY == 1 || next.TileStride == next.RowStride * TileEdge);
        next.Geometry = geometry;
        next.Storage.resize(layout.StorageBytes / sizeof(T));
        next.Fill(value);
        swap(next);
    }
    void swap(ChunkedGrid &other) noexcept {
        using std::swap;
        swap(Layout, other.Layout); swap(Geometry, other.Geometry); Storage.swap(other.Storage);
        swap(RowStride, other.RowStride); swap(TileStride, other.TileStride); swap(WidthShift, other.WidthShift);
        swap(Linear, other.Linear);
    }
    size_t size() const { return size_t(Layout.Width) * Layout.Height; }
    bool empty() const { return size() == 0; }
    size_t UsedBytes() const { return size() * sizeof(T); }
    size_t ReservedBytes() const { return Storage.ReservedBytes(); }
    uint32_t Width() const { return Layout.Width; }
    uint32_t Height() const { return Layout.Height; }
    const GridLayout &GetLayout() const { return Layout; }
    const GridGeometry &GetGeometry() const { return Geometry; }
    void SetGeometry(GridGeometry geometry) { geometry.Validate(); Geometry = geometry; }
    const void *StorageAddress() const { return Storage.data(); }
    std::span<const std::byte> StorageBytes() const { return std::as_bytes(std::span<const T>(Storage)); }

    T &At(uint32_t x, uint32_t y) { assert(x < Width() && y < Height()); return Storage[PhysicalIndex(x, y)]; }
    const T &At(uint32_t x, uint32_t y) const { assert(x < Width() && y < Height()); return Storage[PhysicalIndex(x, y)]; }
    T &operator[](size_t i) { return Storage[LogicalIndex(i)]; }
    const T &operator[](size_t i) const { return Storage[LogicalIndex(i)]; }
    GridAddress Address(uint32_t x, uint32_t y) const {
        assert(x < Width() && y < Height());
        return GridAddress(PhysicalIndex(x, y));
    }
    T &operator[](GridAddress address) { assert(address.Offset < Storage.size()); return Storage[address.Offset]; }
    const T &operator[](GridAddress address) const { assert(address.Offset < Storage.size()); return Storage[address.Offset]; }
    T &at(GridAddress address) { return Storage.at(address.Offset); }
    const T &at(GridAddress address) const { return Storage.at(address.Offset); }
    T &at(size_t i) { if (i >= size()) throw std::out_of_range("grid index"); return (*this)[i]; }
    const T &at(size_t i) const { if (i >= size()) throw std::out_of_range("grid index"); return (*this)[i]; }
    const T &AtWorld(int64_t x, int64_t y) const {
        const auto sample = Geometry.SampleCell(x, y);
        if (sample[0] < 0 || sample[1] < 0 || uint64_t(sample[0]) >= Width() || uint64_t(sample[1]) >= Height())
            throw std::out_of_range("world coordinate outside field");
        return At(uint32_t(sample[0]), uint32_t(sample[1]));
    }

    template<bool Constant>
    class Iterator {
        using Grid = std::conditional_t<Constant, const ChunkedGrid, ChunkedGrid>;
        Grid *Owner = nullptr;
        ptrdiff_t Position = 0;
      public:
        using value_type = T;
        using difference_type = ptrdiff_t;
        using reference = std::conditional_t<Constant, const T &, T &>;
        using pointer = std::conditional_t<Constant, const T *, T *>;
        using iterator_category = std::random_access_iterator_tag;
        Iterator() = default;
        Iterator(Grid *owner, ptrdiff_t position) : Owner(owner), Position(position) {}
        reference operator*() const { return (*Owner)[size_t(Position)]; }
        pointer operator->() const { return &**this; }
        reference operator[](difference_type offset) const { return *(*this + offset); }
        Iterator &operator++() { ++Position; return *this; }
        Iterator operator++(int) { auto old = *this; ++*this; return old; }
        Iterator &operator--() { --Position; return *this; }
        Iterator operator--(int) { auto old = *this; --*this; return old; }
        Iterator &operator+=(difference_type offset) { Position += offset; return *this; }
        Iterator &operator-=(difference_type offset) { Position -= offset; return *this; }
        friend Iterator operator+(Iterator it, difference_type n) { return it += n; }
        friend Iterator operator+(difference_type n, Iterator it) { return it += n; }
        friend Iterator operator-(Iterator it, difference_type n) { return it -= n; }
        friend difference_type operator-(Iterator a, Iterator b) { assert(a.Owner == b.Owner); return a.Position - b.Position; }
        bool operator==(const Iterator &) const = default;
        auto operator<=>(const Iterator &other) const { assert(Owner == other.Owner); return Position <=> other.Position; }
    };
    auto begin() { return Iterator<false>(this, 0); }
    auto end() { return Iterator<false>(this, ptrdiff_t(size())); }
    auto begin() const { return Iterator<true>(this, 0); }
    auto end() const { return Iterator<true>(this, ptrdiff_t(size())); }

    // A view can straddle tile boundaries. The common one-tile path is a span.
    class ConstRow {
        const ChunkedGrid *Owner;
        const T *Direct;
        uint32_t X, Y, Count;
      public:
        ConstRow(const ChunkedGrid &owner, uint32_t x, uint32_t y, uint32_t count)
            : Owner(&owner), Direct(count && count <= owner.Layout.TileWidth - x % owner.Layout.TileWidth ? &owner.At(x, y) : nullptr),
              X(x), Y(y), Count(count) {}
        size_t size() const { return Count; }
        const T &operator[](size_t i) const { assert(i < Count); return Direct ? Direct[i] : Owner->At(X + uint32_t(i), Y); }
    };
    ConstRow Row(uint32_t x, uint32_t y, uint32_t count) const {
        if (y >= Height() || x > Width() || count > Width() - x) throw std::out_of_range("grid row");
        return {*this, x, y, count};
    }
    std::span<T> RowSegment(uint32_t x, uint32_t y) {
        if (x >= Width() || y >= Height()) throw std::out_of_range("grid row segment");
        return {&At(x, y), std::min(TileEdge - x % TileEdge, Width() - x)};
    }
    std::span<const T> RowSegment(uint32_t x, uint32_t y) const {
        if (x >= Width() || y >= Height()) throw std::out_of_range("grid row segment");
        return {&At(x, y), std::min(TileEdge - x % TileEdge, Width() - x)};
    }

    template<class Visitor>
    void ForEachTile(Visitor &&visitor) const {
        for (uint32_t ty = 0; ty < Layout.TilesY; ++ty)
            for (uint32_t tx = 0; tx < Layout.TilesX; ++tx) {
                const uint32_t x = tx * Layout.TileWidth, y = ty * Layout.TileHeight;
                visitor(x, y, std::min(Layout.TileWidth, Width() - x), std::min(Layout.TileHeight, Height() - y));
            }
    }
    void Fill(const T &value) {
        // Padding samples are value-initialized with the allocation; subsequent
        // fills only touch live cells, so edge tiles never contain stale values.
        ForEachTile([&](uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
            for (uint32_t row = 0; row < h; ++row) std::fill_n(&At(x, y + row), w, value);
        });
    }
    void CopyTileFrom(const ChunkedGrid &source, uint32_t tx, uint32_t ty) {
        if (Width() != source.Width() || Height() != source.Height() || tx >= Layout.TilesX || ty >= Layout.TilesY)
            throw std::invalid_argument("grid tile copy mismatch");
        const auto x = tx * Layout.TileWidth, y = ty * Layout.TileHeight;
        const auto width = std::min(Layout.TileWidth, Width() - x), height = std::min(Layout.TileHeight, Height() - y);
        for (uint32_t row = 0; row < height; ++row) std::copy_n(&source.At(x, y + row), width, &At(x, y + row));
    }
    // Wire/image formats use logical rows and explicit pitch, never host tiling.
    void CopyToRows(std::span<T> output, size_t rowStride) const {
        ValidateRows(output.size(), rowStride);
        ForEachTile([&](uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
            for (uint32_t row = 0; row < h; ++row) std::copy_n(&At(x, y + row), w, output.data() + (y + row) * rowStride + x);
        });
    }
    void CopyFromRows(std::span<const T> input, size_t rowStride) {
        ValidateRows(input.size(), rowStride);
        ForEachTile([&](uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
            for (uint32_t row = 0; row < h; ++row) std::copy_n(input.data() + (y + row) * rowStride + x, w, &At(x, y + row));
        });
    }
    void CopyRowTo(uint32_t y, std::span<T> output) const {
        if (y >= Height() || output.size() != Width()) throw std::out_of_range("grid export row");
        for (size_t x = 0; x < Width(); x += Layout.TileWidth)
            std::copy_n(&At(uint32_t(x), y), std::min(Layout.TileWidth, Width() - uint32_t(x)), output.data() + x);
    }
    void CopyRowFrom(uint32_t y, std::span<const T> input) {
        if (y >= Height() || input.size() != Width()) throw std::out_of_range("grid import row");
        for (size_t x = 0; x < Width(); x += Layout.TileWidth)
            std::copy_n(input.data() + x, std::min(Layout.TileWidth, Width() - uint32_t(x)), &At(uint32_t(x), y));
    }

  private:
    GridLayout Layout;
    GridGeometry Geometry;
    PageBuffer<T> Storage;
    size_t RowStride = 0, TileStride = 0;
    int WidthShift = -1;
    bool Linear = false;
    size_t PhysicalIndex(uint32_t x, uint32_t y) const {
        return (size_t(y / TileEdge) * Layout.TilesX + x / TileEdge) * TileStride
            + (y % TileEdge) * RowStride + x % TileEdge;
    }
    size_t LogicalIndex(size_t i) const {
        assert(i < size());
        if (Linear) return i;
        if (WidthShift >= 0) return PhysicalIndex(uint32_t(i & (Width() - 1)), uint32_t(i >> WidthShift));
        return PhysicalIndex(uint32_t(i % Width()), uint32_t(i / Width()));
    }
    void ValidateRows(size_t count, size_t stride) const {
        const auto prefix = empty() ? 0 : CheckedProduct(Height() - 1, stride);
        if (prefix > size_t(PTRDIFF_MAX) - Width()) throw std::length_error("grid transfer size overflow");
        const auto needed = empty() ? 0 : prefix + Width();
        if (stride < Width() || count < needed) throw std::invalid_argument("grid transfer pitch or capacity");
    }
};

// Preserves logical row order for floating-point reductions, while moving all
// address calculation out of the scalar inner loop. All fields share a tile size.
template<class Visitor, class First, class... Others>
void ForEachGridRow(Visitor &&visitor, First &first, Others &...others) {
    static_assert(((First::TileSize == Others::TileSize) && ...));
    if (((first.Width() != others.Width() || first.Height() != others.Height()) || ...))
        throw std::invalid_argument("grid row traversal shape mismatch");
    for (uint32_t y = 0; y < first.Height(); ++y)
        for (size_t x = 0; x < first.Width(); x += First::TileSize)
            visitor(first.RowSegment(uint32_t(x), y), others.RowSegment(uint32_t(x), y)...);
}
} // namespace pixel::memory
