#pragma once

#include "SessionSaveData.h"
#include <filesystem>
#include <optional>

namespace pixel {
// File organization and metadata only. The caller saves chunks before metadata
// and selects a new current world only after its initial save has succeeded.
class SessionStore {
  public:
    SessionStore(); // PIXEL_SAVE_ROOT, or Saves when unset/empty.
    explicit SessionStore(std::filesystem::path root);

    std::optional<std::filesystem::path> CurrentDirectory() const;
    std::filesystem::path CreateWorldDirectory(const WorldGenerationSettings &generation) const;
    void SelectWorld(const std::filesystem::path &directory) const;
    SessionSaveData ReadMetadata(const std::filesystem::path &directory) const;
    void WriteMetadata(const std::filesystem::path &directory, const SessionSaveData &data) const;

  private:
    std::filesystem::path Root;
};
} // namespace pixel
