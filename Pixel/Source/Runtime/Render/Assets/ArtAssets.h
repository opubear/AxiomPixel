#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
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
    size_t CachedImageCount() const { return Images.size(); }

  private:
    struct Manifest;
    ContentReader Read;
    std::unordered_map<std::string, std::shared_ptr<const ArtImage>> Images;
    std::unordered_map<std::string, std::shared_ptr<const BackgroundAssets>> Backgrounds;
    std::unordered_map<std::string, std::shared_ptr<const TerrainAssets>> Terrains;
    std::unordered_map<std::string, std::shared_ptr<const CharacterAssets>> Characters;
    std::unordered_map<std::string, std::shared_ptr<const Manifest>> Manifests;
    std::shared_ptr<const ArtImage> LoadImage(const std::string &path);
    std::shared_ptr<const Manifest> LoadManifest(const std::string &path);
};
} // namespace pixel
