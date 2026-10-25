#include "FileIO.h"
#include "StorageError.h"
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>

namespace pixel::io {
void RequireDirectory(const std::filesystem::path &path) {
    if (!std::filesystem::is_directory(std::filesystem::symlink_status(path)))
        throw StorageError("missing or invalid save directory: " + path.string());
}

void RequireFile(const std::filesystem::path &path) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)))
        throw StorageError("missing or invalid checkpoint file: " + path.string());
}

void EnsureDirectory(const std::filesystem::path &path) {
    std::filesystem::create_directory(path);
    RequireDirectory(path);
}

void CommitFile(const std::filesystem::path &path, const std::function<void(std::ostream &)> &write) {
    const auto temporary = std::filesystem::path(path.string() + ".tmp");
    const auto status = std::filesystem::symlink_status(temporary);
    if (std::filesystem::exists(status) && (!std::filesystem::is_regular_file(status)
        || std::filesystem::hard_link_count(temporary) != 1))
        throw StorageError("invalid temporary save file: " + temporary.string());
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << std::setprecision(std::numeric_limits<double>::max_digits10);
    write(output);
    output.close();
    std::filesystem::rename(temporary, path);
}

FileDigest DigestFile(const std::filesystem::path &path) {
    RequireFile(path);
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::badbit);
    if (!input) throw StorageError("cannot read checkpoint file: " + path.string());
    FileDigest digest;
    std::array<char, 64 * 1024> buffer;
    while (input) {
        input.read(buffer.data(), buffer.size());
        const auto count = input.gcount();
        digest.Bytes += uint64_t(count);
        for (std::streamsize i = 0; i < count; ++i) {
            digest.Hash ^= static_cast<unsigned char>(buffer[size_t(i)]);
            digest.Hash *= 1099511628211ull;
        }
    }
    if (!input.eof()) throw StorageError("cannot read checkpoint file: " + path.string());
    return digest;
}

} // namespace pixel::io
