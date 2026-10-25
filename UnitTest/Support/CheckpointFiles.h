#pragma once
#include "Runtime/Gameplay/Persistence/SessionStore.h"

namespace pixel::testing {
inline std::filesystem::path CheckpointDirectory(const std::filesystem::path &world) {
    return SessionStore(world.parent_path()).ResolveCheckpoint(world);
}
// Convert a disposable test world to the layout of a pristine legacy save.
inline void ExportLegacyFixture(const std::filesystem::path &world) {
    const auto checkpoint = CheckpointDirectory(world);
    std::filesystem::copy_file(checkpoint / "world.meta", world / "world.meta");
    std::filesystem::copy(checkpoint / "chunks", world / "chunks", std::filesystem::copy_options::recursive);
    std::filesystem::remove(world / "checkpoint.current");
    std::filesystem::remove_all(world / "checkpoints");
}
} // namespace pixel::testing
