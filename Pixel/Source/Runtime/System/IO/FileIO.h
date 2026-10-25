#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <ostream>

namespace pixel::io {
struct FileDigest {
    uint64_t Bytes = 0;
    uint64_t Hash = 14695981039346656037ull;
    bool operator==(const FileDigest &) const = default;
};

void RequireDirectory(const std::filesystem::path &path);
void RequireFile(const std::filesystem::path &path);
void EnsureDirectory(const std::filesystem::path &path);
FileDigest DigestFile(const std::filesystem::path &path);
// Publish only after the writer succeeds and closes the temporary file. The
// caller owns the format and directory; this does not promise fsync durability.
void CommitFile(const std::filesystem::path &path, const std::function<void(std::ostream &)> &write);
} // namespace pixel::io
