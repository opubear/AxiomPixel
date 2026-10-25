#pragma once
#include <filesystem>
#include <ios>
#include <optional>
#include <stdexcept>
#include <string>

namespace pixel::io {
// Corrupt persisted data or storage capacity required to load it.
// Programmer errors and allocation failures must not be converted to this type.
class StorageError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Catch storage failures at an I/O boundary; programming errors still propagate.
template <class Operation>
std::optional<std::string> CaptureFailure(const Operation &operation) {
    try {
        operation();
        return {};
    } catch (const StorageError &error) {
        return error.what();
    } catch (const std::filesystem::filesystem_error &error) {
        return error.what();
    } catch (const std::ios_base::failure &error) {
        return error.what();
    }
}
} // namespace pixel::io
