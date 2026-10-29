#pragma once
#include "ChunkedGrid.h"

namespace pixel::memory {
inline size_t TransferPitch(size_t bytes, size_t elementBytes, size_t alignment) {
    if (!std::has_single_bit(alignment)) throw std::invalid_argument("GPU transfer alignment must be a power of two");
    return AlignBytes(bytes, CheckedProduct(alignment / std::gcd(alignment, elementBytes), elementBytes));
}
// An owned staging image; retain it (and any source read lease for direct
// uploads) until the GPU fence signals. This type has no RHI dependency and
// makes no claim that CPU page alignment permits zero-copy device import.
template<class T>
class GridTransfer {
  public:
    template<uint32_t Edge>
    explicit GridTransfer(const ChunkedGrid<T, Edge> &source, size_t rowAlignment = 256)
        : Width(source.Width()), Height(source.Height()), Geometry(source.GetGeometry()),
          RowPitchBytes(TransferPitch(CheckedProduct(Width, sizeof(T)), sizeof(T), rowAlignment)) {
        Storage.resize(CheckedProduct(RowPitchBytes / sizeof(T), Height));
        source.CopyToRows(Storage, RowPitchBytes / sizeof(T));
    }
    std::span<const std::byte> Bytes() const { return std::as_bytes(std::span<const T>(Storage)); }
    std::span<std::byte> WritableBytes() { return std::as_writable_bytes(std::span<T>(Storage)); }
    template<uint32_t Edge>
    void CopyTo(ChunkedGrid<T, Edge> &target) const {
        if (target.Width() != Width || target.Height() != Height || target.GetGeometry() != Geometry)
            throw std::invalid_argument("GPU result domain mismatch");
        target.CopyFromRows(Storage, RowPitchBytes / sizeof(T));
    }
    const uint32_t Width, Height;
    const GridGeometry Geometry;
    const size_t RowPitchBytes;
  private:
    PageBuffer<T> Storage;
};

// GPU lanes typically process adjacent X samples of one component. This
// explicit AoS -> planar transfer supports records such as LBM D2Q9 without
// requiring a CPU algorithm to adopt the GPU's component-major representation.
template<class Scalar, size_t Components>
class PlanarGridTransfer {
    static_assert(Components > 0 && std::is_arithmetic_v<Scalar> && !std::is_same_v<Scalar, bool>);
  public:
    template<uint32_t Edge>
    explicit PlanarGridTransfer(const ChunkedGrid<std::array<Scalar, Components>, Edge> &source,
                                size_t rowAlignment = 256, size_t planeAlignment = 256)
        : Width(source.Width()), Height(source.Height()), Geometry(source.GetGeometry()),
          RowPitchBytes(TransferPitch(CheckedProduct(Width, sizeof(Scalar)), sizeof(Scalar), rowAlignment)),
          PlanePitchBytes(TransferPitch(CheckedProduct(RowPitchBytes, Height), sizeof(Scalar), planeAlignment)) {
        Storage.resize(CheckedProduct(PlanePitchBytes / sizeof(Scalar), Components));
        for (size_t component = 0; component < Components; ++component)
            for (uint32_t y = 0; y < Height; ++y) {
                auto *row = Storage.data() + (component * PlanePitchBytes + y * RowPitchBytes) / sizeof(Scalar);
                const auto input = source.Row(0, y, Width);
                for (uint32_t x = 0; x < Width; ++x) row[x] = input[x][component];
            }
    }
    std::span<const std::byte> Bytes() const { return std::as_bytes(std::span<const Scalar>(Storage)); }
    std::span<std::byte> WritableBytes() { return std::as_writable_bytes(std::span<Scalar>(Storage)); }
    template<uint32_t Edge>
    void CopyTo(ChunkedGrid<std::array<Scalar, Components>, Edge> &target) const {
        if (target.Width() != Width || target.Height() != Height || target.GetGeometry() != Geometry)
            throw std::invalid_argument("GPU planar result domain mismatch");
        for (size_t component = 0; component < Components; ++component)
            for (uint32_t y = 0; y < Height; ++y) {
                const auto *row = Storage.data() + (component * PlanePitchBytes + y * RowPitchBytes) / sizeof(Scalar);
                for (uint32_t x = 0; x < Width; ++x) target.At(x, y)[component] = row[x];
            }
    }
    const uint32_t Width, Height;
    const GridGeometry Geometry;
    const size_t RowPitchBytes, PlanePitchBytes;
  private:
    PageBuffer<Scalar> Storage;
};
} // namespace pixel::memory
