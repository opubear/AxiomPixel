#include "ArtAssets.h"
#include "Asset/Parsers/JsonParser.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include <stb_image.h>

namespace pixel {
struct ArtAssetStore::Manifest {
    nlohmann::json Document;
};

namespace {
constexpr uint32_t MaximumImageEdge = 4096;
constexpr uint32_t MaximumRepeatCells = 1u << 24;
constexpr uint32_t MaximumTerrainLayers = 1024;
constexpr uint32_t MaximumTerrainCellsPerTile = 4096;

void ValidateBackgroundLayout(float heightFraction, size_t layerCount) {
    if (!std::isfinite(heightFraction) || heightFraction < .25f || heightFraction > 2)
        throw std::invalid_argument("Background.HeightFraction must be in [0.25, 2]");
    if (!layerCount || layerCount > 4)
        throw std::invalid_argument("Background.Layers requires one to four layers");
}

std::string ContentPath(std::string_view value) {
    const std::filesystem::path path(value);
    if (value.empty() || value.find('\0') != value.npos || value.find('\\') != value.npos ||
        value.find(':') != value.npos || path.has_root_path())
        throw std::invalid_argument("art paths must be Content-relative: " + std::string(value));

    for (const auto &part : path)
        if (part == "..")
            throw std::invalid_argument("art paths cannot leave Content: " + std::string(value));
    return path.lexically_normal().generic_string();
}

uint32_t PositiveInteger(const nlohmann::json &value, uint32_t maximum, const char *name) {
    if (!value.is_number_integer() || value <= 0 || value > maximum)
        throw std::invalid_argument(std::string(name) + " must be a positive integer in range");
    return value.get<uint32_t>();
}

uint32_t UnsignedInteger(const nlohmann::json &value, const char *name) {
    if (!value.is_number_integer() || value < 0 || value > UINT32_MAX)
        throw std::invalid_argument(std::string(name) + " must be a uint32 integer");
    return value.get<uint32_t>();
}

std::string PngPath(const nlohmann::json &value) {
    const auto path = ContentPath(value.get<std::string>());
    if (std::filesystem::path(path).extension() != ".png")
        throw std::invalid_argument("art images must be PNG: " + path);
    return path;
}
} // namespace

void ValidateBackgroundAssets(const BackgroundAssets &assets) {
    ValidateBackgroundLayout(assets.HeightFraction, assets.Layers.size());

    const auto &first = assets.Layers.front().Image;
    if (bool(assets.BiomeImages[0].Image) != bool(assets.BiomeImages[1].Image))
        throw std::invalid_argument("background requires both desert and snow images");
    for (size_t index = 0; index < assets.LayerCount(); ++index) {
        const auto &layer = assets.Layer(index);
        if (!first || !layer.Image || !layer.RepeatCells || layer.RepeatCells > MaximumRepeatCells ||
            !layer.Image->Width || !layer.Image->Height || layer.Image->Width > MaximumImageEdge || layer.Image->Height > MaximumImageEdge ||
            layer.Image->Width != first->Width || layer.Image->Height != first->Height ||
            layer.Image->Pixels.size() != size_t(layer.Image->Width) * layer.Image->Height * 4)
            throw std::invalid_argument("invalid background image dimensions, data or repeat period");
    }
}

void ValidateTerrainAssets(const TerrainAssets &assets, std::span<const uint32_t> requiredTypes) {
    if (!assets.CellsPerTile || assets.CellsPerTile > MaximumTerrainCellsPerTile ||
        assets.Images.empty() || assets.Images.size() >= MaximumTerrainLayers)
        throw std::invalid_argument("Terrain requires 1..4096 CellsPerTile and 1..1023 image layers");

    std::unordered_set<uint32_t> layers{0};
    const auto &first = assets.Images.front().Image;
    for (const auto &entry : assets.Images) {
        const auto &image = entry.Image;
        if (!entry.Layer || entry.Layer > assets.Images.size() || !layers.insert(entry.Layer).second)
            throw std::invalid_argument("Terrain image layers must be unique and contiguous starting at 1");
        if (!first || !image || !image->Width || !image->Height ||
            image->Width > MaximumImageEdge || image->Height > MaximumImageEdge ||
            image->Width != first->Width || image->Height != first->Height ||
            image->Pixels.size() != size_t(image->Width) * image->Height * 4)
            throw std::invalid_argument("Terrain images require equal dimensions and complete RGBA8 data");
    }

    std::unordered_set<uint32_t> types;
    for (const auto &material : assets.Materials) {
        if (!types.insert(material.TypeId).second || !layers.contains(material.Layer) ||
            !layers.contains(material.SurfaceLayer) || material.SurfaceStrip > 7 || (!material.SurfaceLayer && material.SurfaceStrip) ||
            (material.TypeId == 0 && (material.Layer || material.SurfaceLayer)) ||
            (material.TypeId != 0 && material.Layer == 0))
            throw std::invalid_argument("Terrain materials require unique TypeIds and valid image layers; only air uses layer zero");
    }
    if (!types.contains(0))
        throw std::invalid_argument("Terrain materials must explicitly map air (TypeId 0) to layer zero");
    for (const auto type : requiredTypes)
        if (!types.contains(type))
            throw std::invalid_argument("Terrain is missing required material TypeId " + std::to_string(type));
}

ArtAssetStore::ArtAssetStore(ContentReader reader) : Read(std::move(reader)) {
    if (!Read)
        throw std::invalid_argument("art asset store requires a Content reader");
}

std::shared_ptr<const ArtImage> ArtAssetStore::LoadImage(const std::string &path) {
    if (const auto found = Images.find(path); found != Images.end())
        return found->second;

    const auto bytes = Read(path);
    if (bytes.empty() || bytes.size() > size_t(std::numeric_limits<int>::max()))
        throw std::runtime_error("invalid PNG byte count: " + path);
    const auto *data = reinterpret_cast<const stbi_uc *>(bytes.data());
    int width = 0, height = 0, channels = 0;
    // Bound allocation before decoding; the selected 2K layers fit this limit.
    if (!stbi_info_from_memory(data, int(bytes.size()), &width, &height, &channels) ||
        width <= 0 || height <= 0 || width > MaximumImageEdge || height > MaximumImageEdge)
        throw std::runtime_error("invalid PNG or dimensions exceed 4096: " + path);

    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> decoded(
        stbi_load_from_memory(data, int(bytes.size()), &width, &height, &channels, 4), stbi_image_free);
    if (!decoded)
        throw std::runtime_error("failed to decode PNG: " + path);

    auto image = std::make_shared<ArtImage>();
    image->Width = uint32_t(width);
    image->Height = uint32_t(height);
    const auto *first = reinterpret_cast<const std::byte *>(decoded.get());
    image->Pixels.assign(first, first + size_t(width) * height * 4);
    Images.emplace(path, image);
    return image;
}

std::shared_ptr<const ArtAssetStore::Manifest> ArtAssetStore::LoadManifest(const std::string &path) {
    if (const auto found = Manifests.find(path); found != Manifests.end())
        return found->second;

    auto document = std::make_shared<Manifest>();
    JsonParser::Parse(Read(path), [&](const auto &json) {
        if (!json.at("Version").is_number_integer() || json.at("Version") != 1)
            throw std::invalid_argument("unsupported art manifest Version");
        document->Document = json;
    });
    Manifests.emplace(path, document);
    return document;
}

std::shared_ptr<const BackgroundAssets> ArtAssetStore::LoadBackground(std::string_view manifest) {
    const auto key = ContentPath(manifest);
    if (const auto found = Backgrounds.find(key); found != Backgrounds.end())
        return found->second;

    try {
        auto background = std::make_shared<BackgroundAssets>();
        const auto document = LoadManifest(key);
        const auto &json = document->Document;
        {
            const auto &settings = json.at("Background");
            background->HeightFraction = settings.at("HeightFraction").template get<float>();
            const auto &layers = settings.at("Layers");
            if (!layers.is_array())
                throw std::invalid_argument("Background.Layers must be an array");
            ValidateBackgroundLayout(background->HeightFraction, layers.size());

            for (const auto &layer : layers) {
                const auto path = PngPath(layer.at("Path"));
                const auto repeat = PositiveInteger(layer.at("RepeatCells"), MaximumRepeatCells, "RepeatCells");
                auto image = LoadImage(path);
                background->Layers.push_back({std::move(image), repeat});
            }
            if (settings.contains("Biomes")) {
                const auto &biomes = settings.at("Biomes");
                if (!biomes.is_object() || biomes.size() != 2)
                    throw std::invalid_argument("Background.Biomes requires desert and snow_mountain");
                size_t index = 0;
                for (const auto *name : {"desert", "snow_mountain"}) {
                    const auto &entry = biomes.at(name);
                    background->BiomeImages[index++] = {LoadImage(PngPath(entry.at("Path"))),
                        PositiveInteger(entry.at("RepeatCells"), MaximumRepeatCells, "RepeatCells")};
                }
            }
        }

        ValidateBackgroundAssets(*background);
        Backgrounds.emplace(key, background);
        return background;
    } catch (const std::exception &error) {
        throw std::runtime_error("art manifest " + key + ": " + error.what());
    }
}

std::shared_ptr<const TerrainAssets> ArtAssetStore::LoadTerrain(std::string_view manifest, std::span<const uint32_t> requiredTypes) {
    const auto key = ContentPath(manifest);
    try {
        if (const auto found = Terrains.find(key); found != Terrains.end()) {
            ValidateTerrainAssets(*found->second, requiredTypes);
            return found->second;
        }

        auto terrain = std::make_shared<TerrainAssets>();
        const auto document = LoadManifest(key);
        const auto &settings = document->Document.at("Terrain");
        terrain->CellsPerTile = PositiveInteger(settings.at("CellsPerTile"), MaximumTerrainCellsPerTile, "Terrain.CellsPerTile");
        const auto &images = settings.at("Images");
        const auto &materials = settings.at("Materials");
        if (!images.is_array() || !materials.is_array() || images.empty() || images.size() >= MaximumTerrainLayers)
            throw std::invalid_argument("Terrain.Images and Terrain.Materials must be arrays with 1..1023 images");

        for (const auto &entry : images) {
            const auto layer = PositiveInteger(entry.at("Layer"), MaximumTerrainLayers - 1, "Terrain image Layer");
            terrain->Images.push_back({layer, LoadImage(PngPath(entry.at("Path")))});
        }
        for (const auto &entry : materials)
            terrain->Materials.push_back({UnsignedInteger(entry.at("TypeId"), "Terrain material TypeId"),
                                          UnsignedInteger(entry.at("Layer"), "Terrain material Layer"),
                                          entry.contains("SurfaceLayer") ? UnsignedInteger(entry.at("SurfaceLayer"), "Terrain SurfaceLayer") : 0,
                                          entry.contains("SurfaceStrip") ? UnsignedInteger(entry.at("SurfaceStrip"), "Terrain SurfaceStrip") : 0});

        ValidateTerrainAssets(*terrain, requiredTypes);
        Terrains.emplace(key, terrain);
        return terrain;
    } catch (const std::exception &error) {
        throw std::runtime_error("art manifest " + key + ": " + error.what());
    }
}
void ValidateCharacterAssets(const CharacterAssets &assets) {
    if (assets.Layers.empty() || assets.Layers.size() > 8 || !assets.FrameSize[0] || !assets.FrameSize[1] ||
        !std::isfinite(assets.PixelsPerCell) || assets.PixelsPerCell < 1 || assets.PixelsPerCell > 256)
        throw std::invalid_argument("Character requires 1..8 layers, positive frame size and PixelsPerCell in [1,256]");
    for (size_t axis = 0; axis < 2; ++axis)
        if (!std::isfinite(assets.FootAnchor[axis]) || assets.FootAnchor[axis] < 0 || assets.FootAnchor[axis] > assets.FrameSize[axis])
            throw std::invalid_argument("Character.FootAnchor must lie within the frame");

    const auto &first = assets.Layers.front();
    for (const auto &image : assets.Layers) {
        if (!first || !image || !image->Width || !image->Height ||
            image->Width > MaximumImageEdge || image->Height > MaximumImageEdge ||
            image->Width != first->Width || image->Height != first->Height ||
            image->Width % assets.FrameSize[0] || image->Height % assets.FrameSize[1] ||
            image->Pixels.size() != size_t(image->Width) * image->Height * 4)
            throw std::invalid_argument("Character layers require equal RGBA8 dimensions divisible by FrameSize");
    }
    const uint32_t columns = first->Width / assets.FrameSize[0], rows = first->Height / assets.FrameSize[1];
    for (const auto &clip : assets.Animations)
        if (clip.Row >= rows || clip.FirstFrame >= columns || !clip.FrameCount || clip.FrameCount > columns - clip.FirstFrame ||
            !std::isfinite(clip.FramesPerSecond) || clip.FramesPerSecond <= 0 || clip.FramesPerSecond > 60)
            throw std::invalid_argument("Character animation exceeds its sheet or has invalid frame rate");
}

std::shared_ptr<const CharacterAssets> ArtAssetStore::LoadCharacter(std::string_view manifest) {
    const auto key = ContentPath(manifest);
    if (const auto found = Characters.find(key); found != Characters.end())
        return found->second;
    try {
        auto character = std::make_shared<CharacterAssets>();
        const auto document = LoadManifest(key);
        const auto &settings = document->Document.at("Character");
        const auto &size = settings.at("FrameSize");
        if (!size.is_array() || size.size() != 2)
            throw std::invalid_argument("Character.FrameSize must contain width and height");
        for (size_t axis = 0; axis < 2; ++axis)
            character->FrameSize[axis] = PositiveInteger(size[axis], MaximumImageEdge, "Character.FrameSize");
        character->FootAnchor = settings.at("FootAnchor").get<std::array<float, 2>>();
        character->PixelsPerCell = settings.at("PixelsPerCell").get<float>();
        const auto &layers = settings.at("Layers");
        if (!layers.is_array() || layers.empty() || layers.size() > 8)
            throw std::invalid_argument("Character.Layers requires one to eight layers");
        for (const auto &layer : layers)
            character->Layers.push_back(LoadImage(PngPath(layer.at("Path"))));

        constexpr std::array names{"Idle", "Run", "Jump", "Fall"};
        for (size_t index = 0; index < names.size(); ++index) {
            const auto &clip = settings.at("Animations").at(names[index]);
            character->Animations[index] = {UnsignedInteger(clip.at("Row"), "Character animation Row"),
                UnsignedInteger(clip.at("FirstFrame"), "Character animation FirstFrame"),
                PositiveInteger(clip.at("FrameCount"), MaximumImageEdge, "Character animation FrameCount"),
                clip.at("FramesPerSecond").get<float>(), clip.at("Loop").get<bool>()};
        }
        ValidateCharacterAssets(*character);
        Characters.emplace(key, character);
        return character;
    } catch (const std::exception &error) {
        Manifests.erase(key); // A corrected character configuration can be retried.
        throw std::runtime_error("art manifest " + key + ": " + error.what());
    }
}
std::shared_ptr<const MonsterAssets> ArtAssetStore::LoadMonsters(std::string_view manifest) {
    const auto key = ContentPath(manifest);
    if (const auto found = Monsters.find(key); found != Monsters.end()) return found->second;
    try {
        auto assets = std::make_shared<MonsterAssets>();
        struct Sheet { std::shared_ptr<const ArtImage> Image; size_t Monster; std::string Clip; uint32_t Count; };
        std::vector<Sheet> sheets;
        uint32_t frameWidth = 0, frameHeight = 0, count = 0;
        const auto document = LoadManifest(key);
        const auto &entries = document->Document.at("Monsters");
        if (!entries.is_array() || entries.empty() || entries.size() > 32)
            throw std::invalid_argument("Monsters requires 1..32 definitions");
        for (const auto &entry : entries) {
            MonsterVisual visual;
            visual.Id = entry.at("Id").get<std::string>();
            const auto &v = entry.at("Visual");
            const auto &size = v.at("FrameSize");
            if (!size.is_array() || size.size() != 2) throw std::invalid_argument("monster FrameSize requires two integers");
            for (size_t axis = 0; axis < 2; ++axis) visual.FrameSize[axis] = PositiveInteger(size[axis], 256, "monster frame");
            visual.FootAnchor = v.at("FootAnchor").get<std::array<float, 2>>();
            visual.PixelsPerCell = v.at("PixelsPerCell").get<float>();
            visual.States = v.at("States").get<std::map<std::string, std::string>>();
            frameWidth = std::max(frameWidth, visual.FrameSize[0]);
            frameHeight = std::max(frameHeight, visual.FrameSize[1]);
            const auto &clips = v.at("Clips");
            if (!clips.is_object() || clips.empty() || clips.size() > 32) throw std::invalid_argument("invalid monster clips");
            for (const auto &[name, c] : clips.items()) {
                auto image = LoadImage(PngPath(c.at("Path")));
                const auto frames = PositiveInteger(c.at("FrameCount"), 64, "monster FrameCount");
                if (image->Width != frames * visual.FrameSize[0] || image->Height != visual.FrameSize[1])
                    throw std::invalid_argument("monster sheet must be one complete horizontal frame strip");
                count += frames;
                if (count > 512) throw std::invalid_argument("monster atlas exceeds 512 frames");
                visual.Clips.emplace(name, MonsterClip{{}, c.at("FramesPerSecond").get<float>(), c.at("Loop").get<bool>()});
                sheets.push_back({std::move(image), assets->Monsters.size(), name, frames});
            }
            assets->Monsters.push_back(std::move(visual));
        }
        const uint32_t columns = std::min(count, MaximumImageEdge / frameWidth);
        auto atlas = std::make_shared<ArtImage>();
        atlas->Width = columns * frameWidth;
        atlas->Height = ((count + columns - 1) / columns) * frameHeight;
        if (atlas->Height > MaximumImageEdge) throw std::invalid_argument("monster atlas exceeds image limits");
        atlas->Pixels.resize(size_t(atlas->Width) * atlas->Height * 4);
        uint32_t frameIndex = 0;
        for (const auto &sheet : sheets) {
            auto &visual = assets->Monsters[sheet.Monster];
            auto &clip = visual.Clips.at(sheet.Clip);
            const auto width = visual.FrameSize[0], height = visual.FrameSize[1];
            for (uint32_t frame = 0; frame < sheet.Count; ++frame, ++frameIndex) {
                const uint32_t x = frameIndex % columns * frameWidth, y = frameIndex / columns * frameHeight;
                clip.Frames.push_back({x, y, width, height});
                for (uint32_t row = 0; row < height; ++row)
                    std::copy_n(sheet.Image->Pixels.begin() + (size_t(row) * sheet.Image->Width + frame * width) * 4,
                        size_t(width) * 4, atlas->Pixels.begin() + (size_t(y + row) * atlas->Width + x) * 4);
            }
        }
        assets->Atlas = std::move(atlas);
        ValidateMonsterAssets(*assets);
        Monsters.emplace(key, assets);
        return assets;
    } catch (const std::exception &error) {
        Manifests.erase(key);
        throw std::runtime_error("art manifest " + key + ": " + error.what());
    }
}
} // namespace pixel
