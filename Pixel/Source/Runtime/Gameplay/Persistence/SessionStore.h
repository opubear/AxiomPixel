#pragma once

#include "SessionSaveData.h"
#include <filesystem>
#include <optional>
#include <string>

namespace pixel {
// File organization and opaque gameplay payloads only. A session writes chunks
// to its working directory, then publishes a new immutable generation after I/O
// has settled. World selection remains separate from checkpoint publication.
class SessionStore {
  public:
    SessionStore(); // PIXEL_SAVE_ROOT, or Saves when unset/empty.
    explicit SessionStore(std::filesystem::path root);

    std::optional<std::filesystem::path> CurrentDirectory() const;
    std::filesystem::path CreateWorldDirectory(const WorldGenerationSettings &generation) const;
    void SelectWorld(const std::filesystem::path &directory) const;
    SessionSaveData ReadMetadata(const std::filesystem::path &directory) const;
    void WriteMetadata(const std::filesystem::path &directory, const SessionSaveData &data) const;

    // A missing pointer permits raw v2-v5 import only before checkpoint storage
    // exists. Migration first records an explicit legacy pointer, so subsequent
    // pointer loss cannot silently restore the old root metadata.
    std::filesystem::path ResolveCheckpoint(const std::filesystem::path &worldDirectory) const;
    std::filesystem::path CreateWorkingDirectory(const std::filesystem::path &worldDirectory,
        const std::optional<std::filesystem::path> &sourceDirectory = std::nullopt) const;
    std::filesystem::path PublishCheckpoint(const std::filesystem::path &worldDirectory,
        const std::filesystem::path &workingDirectory, const SessionSaveData &metadata,
        const std::string &domainPayload) const;
    std::string ReadGameplayState(const std::filesystem::path &checkpointDirectory) const;

    static constexpr uint64_t MaxGameplayStateBytes = 8 * 1024 * 1024;

  private:
    std::filesystem::path Root;
};
} // namespace pixel
