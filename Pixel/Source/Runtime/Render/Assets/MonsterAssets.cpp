#include "ArtAssets.h"
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace pixel {
const MonsterVisual &MonsterAssets::Require(std::string_view id) const {
    for (const auto &monster : Monsters)
        if (monster.Id == id) return monster;
    throw std::invalid_argument("missing monster visual: " + std::string(id));
}

void ValidateMonsterAssets(const MonsterAssets &assets) {
    const auto &atlas = assets.Atlas;
    if (!atlas || !atlas->Width || !atlas->Height || atlas->Width > 4096 || atlas->Height > 4096
        || atlas->Pixels.size() != size_t(atlas->Width) * atlas->Height * 4 || assets.Monsters.empty() || assets.Monsters.size() > 32)
        throw std::invalid_argument("invalid monster atlas");
    std::unordered_set<std::string> ids;
    for (const auto &m : assets.Monsters) {
        if (m.Id.empty() || !ids.insert(m.Id).second || !m.FrameSize[0] || !m.FrameSize[1]
            || !std::isfinite(m.PixelsPerCell) || m.PixelsPerCell < 1 || m.PixelsPerCell > 256 || m.Clips.empty())
            throw std::invalid_argument("invalid monster visual: " + m.Id);
        for (size_t axis = 0; axis < 2; ++axis)
            if (!std::isfinite(m.FootAnchor[axis]) || m.FootAnchor[axis] < 0 || m.FootAnchor[axis] > m.FrameSize[axis])
                throw std::invalid_argument("monster foot anchor exceeds frame");
        for (const auto *state : {"Idle", "Move", "Attack", "Hurt", "Death"})
            if (!m.States.contains(state)) throw std::invalid_argument("missing monster animation state");
        for (const auto &[state, clip] : m.States)
            if (!m.Clips.contains(clip)) throw std::invalid_argument("monster state references missing clip");
        for (const auto &[name, clip] : m.Clips) {
            if (clip.Frames.empty() || !std::isfinite(clip.FramesPerSecond) || clip.FramesPerSecond <= 0 || clip.FramesPerSecond > 60)
                throw std::invalid_argument("invalid monster clip timing");
            for (const auto &f : clip.Frames)
                if (f[2] != m.FrameSize[0] || f[3] != m.FrameSize[1] || f[2] > atlas->Width || f[3] > atlas->Height
                    || f[0] > atlas->Width - f[2] || f[1] > atlas->Height - f[3])
                    throw std::invalid_argument("monster clip exceeds atlas");
        }
    }
}
} // namespace pixel
