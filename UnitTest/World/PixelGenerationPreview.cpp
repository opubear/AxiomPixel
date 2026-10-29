#include "Runtime/Simulation/World/Generation/WorldGenerator.h"
#include "Runtime/Render/Assets/ArtAssets.h"
#include "Runtime/Render/GamePresentation.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

// Reproducible CPU preview of real generated cells sampled from the runtime
// texture catalog. This diagnostic never creates a save or starts simulation.
int main(int argc, char **argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: PixelGenerationPreview <output-directory>");
        const std::filesystem::path destination(argv[1]);
        std::filesystem::create_directories(destination);
        pixel::ArtAssetStore assets([](std::string_view name) {
            std::ifstream file(std::filesystem::path(PIXEL_TEST_CONTENT) / name, std::ios::binary);
            if (!file) throw std::runtime_error("missing preview Content asset");
            return std::vector<char>{std::istreambuf_iterator<char>(file), {}};
        });
        pixel::GamePresentation presentation;
        const auto catalog = presentation.LoadTextures(assets);
        const pixel::WorldGenerationSettings settings{pixel::WorldGeneratorKind::Natural, 2, 42};
        std::array<bool, 3> found{};
        std::array<int64_t, 3> centers{};
        std::ofstream climate(destination / "climate.csv");
        climate << "x,surface,temperature,moisture,uplift,forest,desert,snow\n";
        for (int64_t x = -16384; x < 16384; x += 16) {
            const auto column = pixel::SampleGenerationColumn(settings, x);
            const auto &e = column.Environment;
            climate << x << ',' << column.SurfaceY << ',' << e.Temperature << ',' << e.Moisture << ',' << e.Uplift
                    << ',' << e.ForestWeight << ',' << e.DesertWeight << ',' << e.SnowWeight << '\n';
            const size_t biome = size_t(e.Biome);
            if (!found[biome] && std::array{e.ForestWeight, e.DesertWeight, e.SnowWeight}[biome] > 62000) {
                found[biome] = true;
                centers[biome] = x;
            }
        }
        for (size_t biome = 0; biome < centers.size(); ++biome) {
            if (!found[biome]) throw std::runtime_error("preview seed lacks a biome");
            const auto column = pixel::SampleGenerationColumn(settings, centers[biome]);
            const pixel::WorldRect bounds{centers[biome] - 256, column.SurfaceY - 96, 512, 384};
            const auto cells = pixel::SampleGeneration(settings, bounds);
            std::array<int64_t, 512> surfaceHeights;
            for (size_t x = 0; x < surfaceHeights.size(); ++x)
                surfaceHeights[x] = pixel::SampleGenerationColumn(settings, bounds.X + x).SurfaceY;
            const auto name = std::array{"forest", "desert", "snow"}[biome];
            std::ofstream image(destination / (std::string(name) + ".ppm"), std::ios::binary);
            image << "P6\n" << bounds.Width << ' ' << bounds.Height << "\n255\n";
            for (uint32_t y = 0; y < bounds.Height; ++y)
                for (uint32_t x = 0; x < bounds.Width; ++x) {
                    const auto material = cells[size_t(y) * bounds.Width + x];
                    std::array<uint8_t, 3> color = bounds.Y + y < surfaceHeights[x]
                        ? std::array<uint8_t, 3>{153, 192, 207} : std::array<uint8_t, 3>{31, 40, 47};
                    if (material != pixel::CellTypeId::Air) {
                        const auto appearance = presentation.Appearances.at(uint32_t(material));
                        const bool surface = appearance.SurfaceLayer && y > 0 && cells[size_t(y - 1) * bounds.Width + x] == pixel::CellTypeId::Air;
                        const auto layer = surface ? appearance.SurfaceLayer : appearance.Layer;
                        const auto phase = [](int64_t value) { return uint32_t(value - pixel::FloorDivide(value, 64) * 64); };
                        const auto tx = phase(bounds.X + x), ty = surface ? appearance.SurfaceStrip * 8 : phase(bounds.Y + y);
                        const size_t offset = (size_t(layer) * 64 * 64 + ty * 64 + tx) * 4;
                        const auto alpha = uint8_t(catalog.Pixels[offset + 3]);
                        for (size_t c = 0; c < 3; ++c)
                            color[c] = uint8_t((uint8_t(catalog.Pixels[offset + c]) * alpha + color[c] * (255 - alpha)) / 255);
                    }
                    image.write(reinterpret_cast<const char *>(color.data()), color.size());
                }
            std::cout << name << ": seed 42, center " << centers[biome] << ", surface " << column.SurfaceY << '\n';
        }
        uint32_t lakes = 0, groundwater = 0;
        for (const auto &water : pixel::PlanGenerationWater(settings, {-16384, 0, 32768, 3072}))
            (water.Kind == pixel::GenerationWaterKind::SurfaceLake ? lakes : groundwater)++;
        std::cout << "Water plans: " << lakes << " lakes, " << groundwater << " groundwater bodies\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
