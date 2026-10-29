#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace pixel::memory {
// A storage contract, not an assertion about the host CPU's cache line size.
inline constexpr size_t TileAlignment = 128;
inline constexpr size_t SpatialTileEdge = 64;
size_t SystemPageSize();

inline size_t CheckedProduct(size_t a, size_t b) {
    if (b && a > size_t(PTRDIFF_MAX) / b)
        throw std::length_error("spatial storage size overflow");
    return a * b;
}

inline size_t AlignBytes(size_t bytes, size_t alignment) {
    if (!alignment || alignment > size_t(PTRDIFF_MAX) || bytes > size_t(PTRDIFF_MAX) - (alignment - 1))
        throw std::length_error("spatial storage alignment overflow");
    return (bytes + alignment - 1) / alignment * alignment;
}

// Exact owned allocation, with no allocator cache surviving its owner. Page
// alignment concerns allocation starts; each T still has its natural stride.
template<class T>
class PageBuffer {
    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
    static_assert(!std::is_same_v<T, bool>);
  public:
    using value_type = T;
    PageBuffer() = default;
    explicit PageBuffer(size_t count) { resize(count); }
    PageBuffer(const PageBuffer &other) : PageBuffer(other.Count) {
        if (Count) std::memcpy(Data, other.Data, Count * sizeof(T));
    }
    PageBuffer(PageBuffer &&other) noexcept { swap(other); }
    PageBuffer &operator=(const PageBuffer &other) {
        if (this == &other) return *this;
        resize(other.Count);
        if (Count) std::memcpy(Data, other.Data, Count * sizeof(T));
        return *this;
    }
    PageBuffer &operator=(PageBuffer &&other) noexcept { PageBuffer next(std::move(other)); swap(next); return *this; }
    ~PageBuffer() { if (Data) ::operator delete(Data, std::align_val_t(Alignment)); }

    static size_t AllocationBytes(size_t count) {
        return AlignBytes(CheckedProduct(count, sizeof(T)), SystemPageSize());
    }
    void resize(size_t count) {
        if (count <= Capacity) {
            if (count > Count) std::fill(Data + Count, Data + count, T{});
            Count = count;
            return;
        }
        PageBuffer next;
        next.Alignment = std::max({SystemPageSize(), TileAlignment, alignof(T)});
        next.Bytes = AllocationBytes(count);
        next.Data = static_cast<T *>(::operator new(next.Bytes, std::align_val_t(next.Alignment)));
        next.Count = next.Capacity = count;
        // Initialize byte padding too; explicit GPU exports must not expose
        // stale allocator contents between records or at a page's end.
        std::memset(static_cast<void *>(next.Data), 0, next.Bytes);
        std::uninitialized_value_construct_n(next.Data, count);
        if (Count) std::memcpy(next.Data, Data, Count * sizeof(T));
        swap(next);
    }
    void assign(size_t count, const T &value) { const T copy = value; resize(count); std::fill(begin(), end(), copy); }
    void clear() { Count = 0; }
    void swap(PageBuffer &other) noexcept {
        using std::swap;
        swap(Data, other.Data); swap(Count, other.Count); swap(Capacity, other.Capacity);
        swap(Bytes, other.Bytes); swap(Alignment, other.Alignment);
    }
    size_t size() const { return Count; }
    size_t capacity() const { return Capacity; }
    size_t ReservedBytes() const { return Bytes; }
    size_t UsedBytes() const { return Count * sizeof(T); }
    bool empty() const { return !Count; }
    T *data() { return Data; }
    const T *data() const { return Data; }
    T *begin() { return Data; }
    const T *begin() const { return Data; }
    T *end() { return Count ? Data + Count : Data; }
    const T *end() const { return Count ? Data + Count : Data; }
    T &operator[](size_t i) { return Data[i]; }
    const T &operator[](size_t i) const { return Data[i]; }
    T &at(size_t i) { if (i >= Count) throw std::out_of_range("page buffer index"); return Data[i]; }
    const T &at(size_t i) const { if (i >= Count) throw std::out_of_range("page buffer index"); return Data[i]; }
    operator std::span<T>() { return {Data, Count}; }
    operator std::span<const T>() const { return {Data, Count}; }

  private:
    T *Data = nullptr;
    size_t Count = 0, Capacity = 0, Bytes = 0, Alignment = alignof(T);
};
} // namespace pixel::memory
