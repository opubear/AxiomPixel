#include "Assets/ArtAssets.h"
#include "PixelRender/BackgroundView.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
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
    throw std::runtime_error("invalid art input was accepted");
}

std::vector<char> ReadContent(std::string_view name) {
    std::ifstream file(std::filesystem::path(PIXEL_TEST_CONTENT) / name, std::ios::binary);
    if (!file)
        throw std::runtime_error("fixture not found: " + std::string(name));
    return {std::istreambuf_iterator<char>(file), {}};
}

void CacheAndValidation() {
    std::map<std::string, size_t> reads;
    std::map<std::string, std::string> replacements;
    pixel::ArtAssetStore store([&](std::string_view name) {
        const std::string key(name);
        ++reads[key];
        if (const auto found = replacements.find(key); found != replacements.end())
            return std::vector<char>(found->second.begin(), found->second.end());
        return ReadContent(name);
    });
    auto assets = store.LoadBackground("ArtAssets.json");
    Check(assets->Layers.size() == 4 && store.CachedImageCount() == 4, "manifest did not preload all four layers");
    for (const auto &layer : assets->Layers)
        Check(layer.Image->Width == 2560 && layer.Image->Height == 1440 && layer.Image->Pixels.size() == 2560 * 1440 * 4,
              "PNG decode dimensions or RGBA byte count");

    auto invalidAssets = *assets;
    invalidAssets.Layers.front().Image.reset();
    Reject([&] { pixel::ValidateBackgroundAssets(invalidAssets); });
    invalidAssets = *assets;
    auto mismatchedImage = std::make_shared<pixel::ArtImage>();
    mismatchedImage->Width = mismatchedImage->Height = 1;
    mismatchedImage->Pixels.assign(4, std::byte{255});
    invalidAssets.Layers.back().Image = mismatchedImage;
    Reject([&] { pixel::ValidateBackgroundAssets(invalidAssets); });

    const auto initialReads = reads;
    Check(store.LoadBackground("./ArtAssets.json") == assets && reads == initialReads, "cached manifest was reread or decoded");

    const auto source = ReadContent("ArtAssets.json");
    const auto json = nlohmann::json::parse(source);
    replacements["alias.json"] = json.dump();
    const auto alias = store.LoadBackground("alias.json");
    for (size_t i = 0; i < 4; ++i) {
        Check(alias->Layers[i].Image == assets->Layers[i].Image, "manifests did not share decoded image ownership");
        Check(reads[json["Background"]["Layers"][i]["Path"].get<std::string>()] == 1, "shared image reread");
    }

    size_t fixture = 0;
    auto reject = [&](auto modify) {
        auto invalid = json;
        modify(invalid);
        const auto name = "bad" + std::to_string(fixture++) + ".json";
        replacements[name] = invalid.dump();
        Reject([&] { store.LoadBackground(name); });
    };
    for (const auto path : {"../ArtRaw/foo.png", "/tmp/foo.png", "C:/foo.png", "Art\\foo.png", "Art/file.jpg", ""})
        reject([&](auto &j) { j["Background"]["Layers"][0]["Path"] = path; });
    reject([](auto &j) { j["Version"] = 2; });
    reject([](auto &j) { j["Version"] = 1.0; });
    reject([](auto &j) { j["Background"]["HeightFraction"] = 0; });
    reject([](auto &j) { j["Background"]["Layers"] = nlohmann::json::array(); });
    reject([](auto &j) { j["Background"]["Layers"][0]["RepeatCells"] = -1; });
    reject([](auto &j) { j["Background"]["Layers"][0]["RepeatCells"] = 1.5; });
    reject([](auto &j) { j["Background"]["Layers"][0]["RepeatCells"] = UINT64_MAX; });
    reject([](auto &j) { j["Background"]["Layers"].push_back(j["Background"]["Layers"][0]); });
    replacements["invalid.png"] = "broken PNG";
    reject([](auto &j) { j["Background"]["Layers"][0]["Path"] = "invalid.png"; });
    Reject([&] { store.LoadBackground("../ArtAssets.json"); });

    replacements["retry.json"] = "{";
    Reject([&] { store.LoadBackground("retry.json"); });
    replacements["retry.json"] = json.dump();
    Check(store.LoadBackground("retry.json")->Layers[0].Image == assets->Layers[0].Image, "failed load poisoned cache");
}

void DistantParallax() {
    pixel::BackgroundAssets assets{{{nullptr, 65536}, {nullptr, 4096}}, .65f};
    GridView2D view;
    view.WorldOriginCells[0] = int64_t{1} << 60;
    view.OriginPixels[0] = 32;
    const auto first = pixel::BackgroundPhases(assets, view, 800);
    view.WorldOriginCells[0] += 1024;
    view.OriginPixels[0] -= 1024 * 16;
    Check(pixel::BackgroundPhases(assets, view, 800) == first, "chunk reference change moved background");
    view.OriginPixels[0] += 16;
    const auto next = pixel::BackgroundPhases(assets, view, 800);
    Check(std::abs(next[0] - first[0] - 2.f / 65536) < 1e-7f, "remote camera lost one-cell parallax motion");
    view.WorldOriginCells[0] = -1;
    view.OriginPixels[0] = -400;
    const auto negative = pixel::BackgroundPhases(assets, view, 800);
    Check(negative[0] > 1.99f && negative[0] < 2, "negative repeat phase was not normalized");
}
} // namespace

int main() {
    try {
        CacheAndValidation();
        DistantParallax();
        std::cout << "Art assets: PNG, manifest validation, cache sharing, retry and remote parallax passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
