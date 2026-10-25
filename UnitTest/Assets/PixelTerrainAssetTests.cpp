#include "Runtime/Render/Assets/ArtAssets.h"
#include "Runtime/Render/GamePresentation.h"
#include "Runtime/Simulation/World/World.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
constexpr std::array<uint32_t, 11> RequiredTypes{0, 10, 20, 21, 30, 40, 41, 42, 43, 44, 50};

void Check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}

template <class F> void Reject(F &&action) {
    try {
        action();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("invalid terrain input was accepted");
}

std::vector<char> ReadFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("fixture not found: " + path.string());
    return {std::istreambuf_iterator<char>(file), {}};
}

std::vector<char> ReadContent(std::string_view name) {
    return ReadFile(std::filesystem::path(PIXEL_TEST_CONTENT) / name);
}

void RuntimeCopies() {
    const auto root = std::filesystem::path(PIXEL_TEST_CONTENT).parent_path().parent_path();
    size_t copies = 0;
    for (const auto group : {"NaturalBlocks", "Elements"}) {
        const auto record = nlohmann::json::parse(ReadContent(std::string("Art/") + group + "/SOURCE.json"));
        Check(record.at("CopiedWithoutModification") == true, "runtime copy lost provenance");
        for (const auto &entry : record.at("Resources")) {
            const auto target = ReadContent(std::string("Art/") + group + "/" + entry.at("Path").get<std::string>());
            const auto source = ReadFile(root / entry.at("Source").get<std::string>());
            Check(target == source, "runtime PNG differs from its preserved ArtRaw source");
            Check(entry.at("Width") == 64 && entry.at("Height") == 64 && entry.at("SHA256").get<std::string>().size() == 64,
                  "runtime copy has incomplete dimensions or source hash");
            ++copies;
        }
    }
    Check(copies == 10, "runtime terrain copies are incomplete");
}

void CatalogAndCache() {
    std::map<std::string, size_t> reads;
    std::map<std::string, std::string> replacements;
    pixel::ArtAssetStore assets([&](std::string_view name) {
        const std::string key(name);
        ++reads[key];
        if (const auto found = replacements.find(key); found != replacements.end())
            return std::vector<char>(found->second.begin(), found->second.end());
        return ReadContent(name);
    });
    const auto terrain = assets.LoadTerrain("ArtAssets.json", RequiredTypes);
    Check(terrain->Images.size() == 10 && terrain->Materials.size() == RequiredTypes.size() && terrain->CellsPerTile == 64,
          "terrain manifest did not load every natural material");
    for (const auto &entry : terrain->Images)
        Check(entry.Image->Width == 64 && entry.Image->Height == 64 && entry.Image->Pixels.size() == 64 * 64 * 4,
              "terrain PNG is not complete 64x64 RGBA");
    const auto initialReads = reads;
    Check(assets.LoadTerrain("./ArtAssets.json", RequiredTypes) == terrain && reads == initialReads,
          "terrain cache reread or decoded a successful load");

    pixel::GamePresentation presentation;
    const auto fallback = presentation.MakeTextures();
    Check(fallback.Width == 1 && fallback.Height == 1 && fallback.Layers == 11 && fallback.CellsPerTile == 1,
          "CPU fallback changed its one-cell texture sampling contract");
    for (const auto type : RequiredTypes)
        Check(presentation.Appearances.contains(type), "default presentation has an unmapped registered material");
    Check(fallback.Pixels[4] == std::byte{151} && fallback.Pixels[5] == std::byte{104} && fallback.Pixels[6] == std::byte{65},
          "fallback dirt colour changed");

    const auto catalog = presentation.LoadTextures(assets);
    ValidateGridTextureCatalog(catalog);
    Check(catalog.Width == 64 && catalog.Height == 64 && catalog.Layers == 11 && catalog.CellsPerTile == 64,
          "Content catalog lost image dimensions or world repeat period");
    const size_t layerBytes = 64 * 64 * 4;
    Check(std::all_of(catalog.Pixels.begin(), catalog.Pixels.begin() + layerBytes, [](auto value) { return value == std::byte{}; }),
          "air layer is not transparent");
    for (const auto &entry : terrain->Images)
        Check(std::equal(entry.Image->Pixels.begin(), entry.Image->Pixels.end(), catalog.Pixels.begin() + entry.Layer * layerBytes),
              "catalog layers do not match decoded Content images");
    Check(presentation.Appearances.at(10).Layer == presentation.Appearances.at(20).Layer &&
          presentation.Appearances.at(21).Layer == 1 && presentation.Appearances.at(21).SurfaceLayer == 2 &&
          presentation.Appearances.at(50).Layer == 9 && presentation.Appearances.at(50).SurfaceLayer == 10 &&
          presentation.Appearances.at(50).SurfaceStrip == 3,
          "body/surface or shared soil mapping was lost");
    const auto &water = presentation.Appearances.at(50);
    bool visibleWaterEdge = false;
    for (size_t y = water.SurfaceStrip * 8; y < (water.SurfaceStrip + 1) * 8; ++y)
        for (size_t x = 0; x < 64; ++x)
            visibleWaterEdge |= catalog.Pixels[(water.SurfaceLayer * 64 * 64 + y * 64 + x) * 4 + 3] != std::byte{};
    Check(visibleWaterEdge, "water surface strip selected the source image's transparent sky");

    const auto background = assets.LoadBackground("ArtAssets.json");
    Check(background->Layers.size() == 4 && reads["ArtAssets.json"] == 1 && assets.CachedImageCount() == 14,
          "background and terrain did not share manifest ownership");
    const auto json = nlohmann::json::parse(ReadContent("ArtAssets.json"));
    replacements["alias.json"] = json.dump();
    const auto alias = assets.LoadTerrain("alias.json", RequiredTypes);
    for (size_t i = 0; i < terrain->Images.size(); ++i)
        Check(alias->Images[i].Image == terrain->Images[i].Image &&
              reads[json["Terrain"]["Images"][i]["Path"].get<std::string>()] == 1,
              "terrain manifests did not share the PNG cache");
    Reject([&] { assets.LoadTerrain("ArtAssets.json", std::array<uint32_t, 1>{999}); });

    size_t fixture = 0;
    auto reject = [&](auto modify) {
        auto invalid = json;
        modify(invalid);
        const auto name = "bad-terrain-" + std::to_string(fixture++) + ".json";
        replacements[name] = invalid.dump();
        Reject([&] { presentation.LoadTextures(assets, name); });
        Check(presentation.Appearances.at(50).SurfaceLayer == 10, "failed catalog load replaced the live appearance table");
    };
    for (const auto path : {"../ArtRaw/foo.png", "/tmp/foo.png", "C:/foo.png", "Art\\foo.png", "Art/file.jpg", ""})
        reject([&](auto &j) { j["Terrain"]["Images"][0]["Path"] = path; });
    reject([](auto &j) { j.erase("Terrain"); });
    reject([](auto &j) { j["Terrain"]["Images"] = nlohmann::json::object(); });
    reject([](auto &j) { j["Terrain"]["Materials"] = nlohmann::json::object(); });
    reject([](auto &j) { j["Terrain"]["Images"] = nlohmann::json::array(); });
    reject([](auto &j) { j["Terrain"]["Images"][0]["Layer"] = 0; });
    reject([](auto &j) { j["Terrain"]["Images"][0]["Layer"] = 11; });
    reject([](auto &j) { j["Terrain"]["Images"][0]["Layer"] = 2; });
    reject([](auto &j) { j["Terrain"]["Images"][0]["Path"] = "Art/NaturalParallax/00_far.png"; });
    reject([](auto &j) { j["Terrain"]["CellsPerTile"] = 0; });
    reject([](auto &j) { j["Terrain"]["CellsPerTile"] = -1; });
    reject([](auto &j) { j["Terrain"]["CellsPerTile"] = 1.5; });
    reject([](auto &j) { j["Terrain"]["CellsPerTile"] = 4097; });
    reject([](auto &j) { j["Terrain"]["Materials"].push_back(j["Terrain"]["Materials"][0]); });
    reject([](auto &j) { j["Terrain"]["Materials"].erase(10); });
    reject([](auto &j) { j["Terrain"]["Materials"].erase(0); });
    reject([](auto &j) { j["Terrain"]["Materials"][0]["Layer"] = 1; });
    reject([](auto &j) { j["Terrain"]["Materials"][1]["Layer"] = 0; });
    reject([](auto &j) { j["Terrain"]["Materials"][1]["Layer"] = 11; });
    reject([](auto &j) { j["Terrain"]["Materials"][3]["SurfaceLayer"] = 11; });
    reject([](auto &j) { j["Terrain"]["Materials"][3]["SurfaceStrip"] = 8; });
    reject([](auto &j) { j["Terrain"]["Materials"][1]["SurfaceStrip"] = 1; });
    reject([](auto &j) { j["Terrain"]["Materials"][1]["TypeId"] = -1; });
    reject([](auto &j) { j["Terrain"]["Materials"][1]["TypeId"] = 10.5; });
    reject([](auto &j) { j["Terrain"]["Materials"][1]["TypeId"] = UINT64_MAX; });
    replacements["broken.png"] = "not a PNG";
    reject([](auto &j) { j["Terrain"]["Images"][0]["Path"] = "broken.png"; });
    Reject([&] { assets.LoadTerrain("../ArtAssets.json"); });
    replacements["retry-terrain.json"] = "{";
    Reject([&] { assets.LoadTerrain("retry-terrain.json"); });
    replacements["retry-terrain.json"] = json.dump();
    Check(assets.LoadTerrain("retry-terrain.json")->Images[0].Image == terrain->Images[0].Image,
          "failed parsing poisoned the terrain cache");

    auto invalid = *terrain;
    invalid.Images[0].Image.reset();
    Reject([&] { pixel::ValidateTerrainAssets(invalid, RequiredTypes); });
    invalid = *terrain;
    auto shortImage = std::make_shared<pixel::ArtImage>(*invalid.Images[0].Image);
    shortImage->Pixels.pop_back();
    invalid.Images[0].Image = shortImage;
    Reject([&] { pixel::ValidateTerrainAssets(invalid, RequiredTypes); });
    auto badCatalog = catalog;
    badCatalog.CellsPerTile = 0;
    Reject([&] { ValidateGridTextureCatalog(badCatalog); });
    badCatalog.CellsPerTile = 4097;
    Reject([&] { ValidateGridTextureCatalog(badCatalog); });
}

void PublishedSurfaces() {
    constexpr uint32_t edge = Grid2D::RegionSize;
    pixel::World world(edge * 3, edge * 3);
    pixel::WorldSnapshot snapshot(edge * 3, edge * 3);
    pixel::GamePresentation presentation;
    WorldGridFrame frame;
    world.SetCell(1, edge, {pixel::CellTypeId::GrassSoil});
    world.SetCell(2, edge, {pixel::CellTypeId::Water});
    world.SetCell(1, edge + 1, {pixel::CellTypeId::GrassSoil});
    world.SetCell(2, edge + 1, {pixel::CellTypeId::Water});
    world.PublishState(snapshot);
    auto publish = [&] { BuildWorldGridFrame(snapshot.Read(), presentation.Appearances, frame); };
    auto at = [&](uint32_t x, uint32_t y) { return frame.Read().Cells[size_t(y) * frame.GetWidth() + x]; };
    publish();
    Check(at(1, edge) == (Grid2D::SurfaceCellFlag | 2) &&
          at(2, edge) == (Grid2D::SurfaceCellFlag | (3u << Grid2D::SurfaceStripShift) | 10),
          "published exposed grass and water did not select their top strips");
    Check(at(1, edge + 1) == 1 && at(2, edge + 1) == 9, "buried grass or water used surface art");
    ValidateWorldGridFrame(frame.Read(), 11, 800, 600);
    const auto lowerVersion = frame.Read().RegionVersions[3];
    const auto unrelatedVersion = frame.Read().RegionVersions[8];
    world.SetCell(1, edge - 1, {pixel::CellTypeId::Stone});
    world.SetCell(2, edge - 1, {pixel::CellTypeId::Stone});
    publish();
    Check(at(1, edge) == (Grid2D::SurfaceCellFlag | 2), "unpublished neighbour edit leaked into surface selection");
    world.PublishState(snapshot);
    publish();
    Check(at(1, edge) == 1 && at(2, edge) == 9 && frame.Read().RegionVersions[3] != lowerVersion,
          "upper region edit did not invalidate the unchanged lower region surface");
    Check(frame.Read().RegionVersions[8] == unrelatedVersion, "surface neighbour invalidation rebuilt unrelated regions");
    world.SetCell(1, edge - 1, {});
    world.PublishState(snapshot);
    publish();
    Check(at(1, edge) == (Grid2D::SurfaceCellFlag | 2), "exposing grass did not restore its surface image");
    presentation.Appearances.at(uint32_t(pixel::CellTypeId::GrassSoil)).SurfaceLayer = 3;
    publish();
    Check(at(1, edge) == (Grid2D::SurfaceCellFlag | 3), "surface mapping edit did not invalidate cached material output");

    // The first published row has no published upper neighbour. Missing space
    // must not be inferred as air, even when the live map has an empty row there.
    pixel::WorldSnapshot clipped;
    clipped.Update(world.GetMap(), {0, edge, edge, edge});
    BuildWorldGridFrame(clipped.Read(), presentation.Appearances, frame);
    Check(frame.Read().Cells[1] == 1 && frame.Read().Cells[2] == 9, "unpublished neighbour was inferred as an exposed surface");
    frame.Cells()[1] = Grid2D::SurfaceCellFlag;
    Reject([&] { ValidateWorldGridFrame(frame.Read(), 11, 800, 600); });
    frame.Cells()[1] = Grid2D::SurfaceCellFlag | 11;
    Reject([&] { ValidateWorldGridFrame(frame.Read(), 11, 800, 600); });
    frame.Cells()[1] = (1u << Grid2D::SurfaceStripShift) | 1;
    Reject([&] { ValidateWorldGridFrame(frame.Read(), 11, 800, 600); });
}
} // namespace

int main() {
    try {
        RuntimeCopies();
        CatalogAndCache();
        PublishedSurfaces();
        std::cout << "Terrain assets: Content PNG copies, JSON validation, shared caches, complete catalogs and published surfaces passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
