#include "Runtime/System/Memory/GridTransfer.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace pixel::memory;
namespace {
void Check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
template<class F> void Reject(F &&action, const char *message) {
    bool rejected = false;
    try { action(); } catch (const std::exception &) { rejected = true; }
    Check(rejected, message);
}

template<class T> void AlignmentAndEdges() {
    for (const auto [width, height] : {std::pair{1u, 1u}, {1u, 129u}, {63u, 65u}, {129u, 97u}, {64u, 128u}}) {
        ChunkedGrid<T> field(width, height);
        Check(uintptr_t(field.StorageAddress()) % SystemPageSize() == 0, "allocation not page aligned");
        Check(field.ReservedBytes() % SystemPageSize() == 0, "reservation not whole pages");
        Check(field.UsedBytes() == size_t(width) * height * sizeof(T), "logical byte accounting includes padding");
        const auto &layout = field.GetLayout();
        Check(layout.StorageBytes <= field.ReservedBytes(), "allocation smaller than tiled layout");
        size_t visited = 0;
        field.ForEachTile([&](uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
            Check(uintptr_t(&field.At(x, y)) % TileAlignment == 0, "tile start not aligned");
            for (uint32_t row = 0; row < h; ++row) {
                Check(uintptr_t(&field.At(x, y + row)) % alignof(T) == 0, "tile row not aligned");
                for (uint32_t col = 0; col < w; ++col) {
                    auto &element = field.At(x + col, y + row);
                    Check(uintptr_t(&element) % alignof(T) == 0, "element not naturally aligned");
                    Check(reinterpret_cast<const std::byte *>(&element) == field.StorageBytes().data() + layout.ByteOffset(x + col, y + row), "GPU descriptor does not locate CPU element");
                    ++visited;
                }
            }
        });
        Check(visited == field.size(), "partial tiles lost/duplicated samples");
    }
}

void LayoutIsolationAndTransfers() {
    const GridGeometry geometry{-(int64_t{1} << 55), int64_t{1} << 55, 4, 4};
    ChunkedGrid<uint32_t, 32> source(129, 97, 0, geometry);
    std::iota(source.begin(), source.end(), 3u);
    const auto row = source.Row(29, 31, 70);
    for (size_t i = 0; i < row.size(); ++i) Check(row[i] == 3 + 31 * 129 + 29 + i, "row crossing tiles changed logical order");
    GridTransfer<uint32_t> exchange(source, 512);
    Check(exchange.RowPitchBytes % 512 == 0, "GPU pitch constraint lost");
    ChunkedGrid<uint32_t> target(129, 97, 0, geometry);
    exchange.CopyTo(target);
    Check(std::equal(source.begin(), source.end(), target.begin()), "different layouts changed transfer values");
    for (uint32_t y = 0; y < source.Height(); ++y) {
        const auto bytes = exchange.Bytes().subspan(y * exchange.RowPitchBytes + source.Width() * sizeof(uint32_t), exchange.RowPitchBytes - source.Width() * sizeof(uint32_t));
        Check(std::all_of(bytes.begin(), bytes.end(), [](std::byte b) { return b == std::byte{}; }), "staging padding leaked uninitialized bytes");
    }
    const uint32_t replacement = 777;
    std::memcpy(exchange.WritableBytes().data() + exchange.RowPitchBytes + sizeof(uint32_t), &replacement, sizeof(replacement));
    exchange.CopyTo(target);
    Check(target.At(1, 1) == replacement && source.At(1, 1) != replacement, "readback aliasing or wrong pitch");
    target.SetGeometry({});
    Reject([&] { exchange.CopyTo(target); }, "readback accepted a different world domain");
    Reject([&] { GridTransfer<uint32_t> bad(source, 3); }, "invalid transfer alignment accepted");
    Reject([&] { GridTransfer<uint32_t> bad(source, size_t{1} << (sizeof(size_t) * 8 - 1)); }, "overflowing transfer alignment accepted");
    Reject([&] { source.CopyToRows({}, source.Width()); }, "truncated transfer accepted");
}

void GeometryAndLifetime() {
    const int64_t origin = int64_t{1} << 55;
    ChunkedGrid<int> coarse(17, 19, 0, {origin, -origin, 4, 4});
    coarse.At(1, 2) = 99;
    Check(coarse.AtWorld(origin + 4, -origin + 8) == 99 && coarse.AtWorld(origin + 7, -origin + 11) == 99, "4x4 logical coverage or far precision lost");
    Check(GridGeometry{0, 0, 4, 4}.SampleCell(-1, -5) == std::array<int64_t, 2>{-1, -2}, "negative sampling must floor");
    Check(GridGeometry{origin, -origin, 1.5, 2.5}.SampleCell(origin + 4, -origin + 4) == std::array<int64_t, 2>{2, 1}, "fractional spacing lost");
    Reject([&] { coarse.AtWorld(origin - 1, -origin); }, "outside sample accepted");
    auto copy = coarse;
    coarse.At(1, 2) = 7;
    Check(copy.At(1, 2) == 99 && copy.StorageAddress() != coarse.StorageAddress(), "copy aliases a published field");
    const auto reusable = copy.StorageAddress();
    copy = coarse;
    Check(copy.StorageAddress() == reusable && copy.At(1, 2) == 7, "same-shape solver copy did not reuse its storage");
    copy.At(1, 2) = 99;
    auto moved = std::move(copy);
    copy.Reset(17, 19, 2);
    Check(moved.At(1, 2) == 99 && copy.At(1, 2) == 2, "moved-from grid cannot be reused");
    auto before = coarse.StorageAddress();
    Reject([&] { coarse.Reset(17, 19, 5, {0, 0, 0, 1}); }, "zero resolution accepted");
    Reject([&] { coarse.Reset(UINT32_MAX, UINT32_MAX); }, "overflow accepted");
    Check(coarse.StorageAddress() == before && coarse.At(1, 2) == 7, "failed resize changed field");
    coarse.Reset(0, 0);
    Check(coarse.empty() && coarse.ReservedBytes() == 0, "empty reset retained allocation");
}
void ComponentExchange() {
    ChunkedGrid<std::array<double, 9>> populations(65, 7);
    for (size_t i = 0; i < populations.size(); ++i)
        for (size_t q = 0; q < 9; ++q) populations[i][q] = double(i * 9 + q);
    PlanarGridTransfer<double, 9> transfer(populations, 256, 4096);
    Check(transfer.RowPitchBytes % 256 == 0 && transfer.PlanePitchBytes % 4096 == 0, "component transfer alignment");
    double value = -1;
    std::memcpy(&value, transfer.Bytes().data() + 3 * transfer.PlanePitchBytes + 2 * transfer.RowPitchBytes + 64 * sizeof(double), sizeof(value));
    Check(value == populations.At(64, 2)[3], "component-major shader indexing mismatch");
    ChunkedGrid<std::array<double, 9>, 16> restored(65, 7);
    transfer.CopyTo(restored);
    Check(std::equal(populations.begin(), populations.end(), restored.begin()), "component transfer roundtrip changed distributions");
}
void SharedAddresses() {
    for (const auto [w, h] : {std::pair{1u, 129u}, {17u, 19u}, {129u, 97u}}) {
        ChunkedGrid<uint8_t> mask(w, h);
        ChunkedGrid<std::array<double, 9>> values(w, h);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                const auto address = mask.Address(x, y);
                values[address][0] = double(y * w + x);
            }
        for (size_t i = 0; i < values.size(); ++i)
            Check(values[i][0] == double(i), "mixed scalar fields do not share a sample address");
    }
}
struct alignas(256) AlignedRecord { uint64_t Values[32]{}; };
} // namespace

int main() {
    try {
        AlignmentAndEdges<uint8_t>(); AlignmentAndEdges<uint32_t>(); AlignmentAndEdges<double>();
        AlignmentAndEdges<std::array<double, 9>>(); AlignmentAndEdges<AlignedRecord>();
        LayoutIsolationAndTransfers(); GeometryAndLifetime(); ComponentExchange(); SharedAddresses();
        std::cout << "Spatial storage, alignment, resolution and GPU exchange passed\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
