#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace pixel {
struct ArtImage {
    uint32_t Width = 0, Height = 0;
    std::vector<std::byte> Pixels; // straight-alpha, sRGB RGBA8
};

struct BackgroundLayer {
    std::shared_ptr<const ArtImage> Image;
    uint32_t RepeatCells = 1; // world distance for a complete mirrored pair
};

struct BackgroundAssets {
    std::vector<BackgroundLayer> Layers; // back to front, at most four
    float HeightFraction = 1;
    // Optional opaque desert and snow panoramas, uploaded after forest layers.
    // Keep resource paths and GPU layer indices out of Simulation.
    std::array<BackgroundLayer, 2> BiomeImages;
    bool HasBiomeImages() const { return bool(BiomeImages[0].Image); }
    size_t LayerCount() const { return Layers.size() + (HasBiomeImages() ? 2 : 0); }
    const BackgroundLayer &Layer(size_t index) const {
        return index < Layers.size() ? Layers.at(index) : BiomeImages.at(index - Layers.size());
    }
};

// Shared contract for manifests and render consumers, including test fixtures.
void ValidateBackgroundAssets(const BackgroundAssets &assets);

struct TerrainImage {
    uint32_t Layer = 0; // zero is reserved for transparent air
    std::shared_ptr<const ArtImage> Image;
};

struct TerrainMaterial {
    uint32_t TypeId = 0;
    uint32_t Layer = 0;
    uint32_t SurfaceLayer = 0; // selected only when the published cell above is air
    uint32_t SurfaceStrip = 0; // which horizontal eighth contains the exposed edge
};

struct TerrainAssets {
    uint32_t CellsPerTile = 64;
    std::vector<TerrainImage> Images;
    std::vector<TerrainMaterial> Materials;
};

// Does not know gameplay types: the presentation boundary supplies required IDs.
void ValidateTerrainAssets(const TerrainAssets &assets, std::span<const uint32_t> requiredTypes = {});

enum class CharacterMotion : size_t { Idle, Run, Jump, Fall, Count };
struct CharacterClip {
    uint32_t Row = 0, FirstFrame = 0, FrameCount = 1;
    float FramesPerSecond = 1;
    bool Loop = true;
};
struct CharacterAssets {
    std::array<uint32_t, 2> FrameSize{};
    std::array<float, 2> FootAnchor{};
    float PixelsPerCell = 1;
    std::vector<std::shared_ptr<const ArtImage>> Layers; // back to front, shared frame grid
    std::array<CharacterClip, size_t(CharacterMotion::Count)> Animations;
};
void ValidateCharacterAssets(const CharacterAssets &assets);

struct MonsterClip {
    std::vector<std::array<uint32_t, 4>> Frames; // atlas x/y/width/height
    float FramesPerSecond = 10;
    bool Loop = false;
};
struct MonsterVisual {
    std::string Id;
    std::array<uint32_t, 2> FrameSize{};
    std::array<float, 2> FootAnchor{};
    float PixelsPerCell = 10.5f;
    std::map<std::string, std::string> States;
    std::map<std::string, MonsterClip> Clips;
};
struct MonsterAssets {
    std::shared_ptr<const ArtImage> Atlas;
    std::vector<MonsterVisual> Monsters;
    const MonsterVisual &Require(std::string_view id) const;
};
void ValidateMonsterAssets(const MonsterAssets &assets);

// Owned by the game. All successful reads/decodes are retained for
// its lifetime. The reader resolves Content-relative paths (Axiom at runtime).
// Loading is synchronous on the application thread, before frame publication.
class ArtAssetStore {
  public:
    using ContentReader = std::function<std::vector<char>(std::string_view)>;
    explicit ArtAssetStore(ContentReader reader);

    std::shared_ptr<const BackgroundAssets> LoadBackground(std::string_view manifest);
    std::shared_ptr<const TerrainAssets> LoadTerrain(std::string_view manifest, std::span<const uint32_t> requiredTypes = {});
    std::shared_ptr<const CharacterAssets> LoadCharacter(std::string_view manifest = "CharacterAssets.json");
    std::shared_ptr<const MonsterAssets> LoadMonsters(std::string_view manifest = "Monsters.json");
    size_t CachedImageCount() const { return Images.size(); }

  private:
    struct Manifest;
    ContentReader Read;
    std::unordered_map<std::string, std::shared_ptr<const ArtImage>> Images;
    std::unordered_map<std::string, std::shared_ptr<const BackgroundAssets>> Backgrounds;
    std::unordered_map<std::string, std::shared_ptr<const TerrainAssets>> Terrains;
    std::unordered_map<std::string, std::shared_ptr<const CharacterAssets>> Characters;
    std::unordered_map<std::string, std::shared_ptr<const MonsterAssets>> Monsters;
    std::unordered_map<std::string, std::shared_ptr<const Manifest>> Manifests;
    std::shared_ptr<const ArtImage> LoadImage(const std::string &path);
    std::shared_ptr<const Manifest> LoadManifest(const std::string &path);
};
} // namespace pixel
