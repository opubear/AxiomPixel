#include "PixelRender/PixelWorldPresentation.h"
#include "World/World.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <class Error, class Action>
void Reject(Action action) {
    try {
        action();
    } catch (const Error &) {
        return;
    }
    throw std::runtime_error("invalid field-view input was accepted");
}
void DirectionColors() {
    struct Example {
        pixel::WindVelocity Direction;
        std::array<float, 3> Color;
    };
    const std::array examples{
        Example{{0, 0}, {0.5f, 0.5f, 0.5f}},
        Example{{1, 0}, {1, 0.5f, 0.5f}},
        Example{{-1, 0}, {0, 0.5f, 0.5f}},
        Example{{0, 1}, {0.5f, 1, 0.5f}},
        Example{{0, -1}, {0.5f, 0, 0.5f}},
        Example{{-1, -1}, {0.14644661f, 0.14644661f, 0.5f}},
        Example{{1, 1}, {0.85355339f, 0.85355339f, 0.5f}},
        Example{{-1, 1}, {0.14644661f, 0.85355339f, 0.5f}},
        Example{{1, -1}, {0.85355339f, 0.14644661f, 0.5f}},
    };
    for (const auto &example : examples) {
        for (float scale : {0.00001f, 1.f, 1000.f, std::numeric_limits<float>::max()}) {
            const auto color = FieldDirectionColor({example.Direction.X * scale, example.Direction.Y * scale});
            for (size_t channel = 0; channel < color.size(); ++channel)
                Check(std::isfinite(color[channel]) && color[channel] >= 0 && color[channel] <= 1
                          && std::abs(color[channel] - example.Color[channel]) < 0.00001f,
                      "direction was not normalized/remapped into the expected colour");
            Check(color[2] == 0.5f, "negative direction became indistinguishable black");
        }
    }
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        Reject<std::invalid_argument>([&] { FieldDirectionColor({invalid, 0}); });
        Reject<std::invalid_argument>([&] { FieldDirectionColor({0, invalid}); });
    }
}

void CombinedFieldSampling() {
    pixel::World world(4, 4);
    Check(world.GetFieldVelocity(1, 1) == pixel::WindVelocity{0, 60}, "base force sampling has incorrect units");
    pixel::WindSettings wind;
    wind.Ambient = {60, -120};
    wind.GustSpeed = 0;
    world.ConfigureWind(2, wind);
    Check(world.GetFieldVelocity(1, 1) == pixel::WindVelocity{60, -60}, "debug sample omitted wind or base force");
    Check(world.GetFieldVelocity(1, 2) == pixel::WindVelocity{0, 60}, "debug field leaked below the wind boundary");
    world.PlaceDirt(1, 1);
    world.Tick(1.f / 60);
    Check(world.GetCell(2, 0).Type == pixel::CellTypes::Dirt, "debug sample disagrees with movement direction");
    Reject<std::out_of_range>([&] { world.GetFieldVelocity(4, 0); });
    world.DisableWind();
    world.FillForceDirection({0, 0});
    Check(world.GetFieldVelocity(1, 1) == pixel::WindVelocity{}, "disabled wind or zero force retained a direction");
}

void FieldFramePublication() {
    pixel::World world(Grid2D::Width, Grid2D::Height);
    world.FillForceDirection({0, 0});
    // Repeat the sign/zero cases across every texture page, on air and material.
    constexpr std::array<pixel::ForceDirection, 9> directions{{{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {-1, -1}, {1, 1}, {-1, 1}, {1, -1}}};
    constexpr std::array<uint32_t, 9> colors{0x808080, 0x8080FF, 0x808000, 0x80FF80, 0x800080, 0x802525, 0x80DADA, 0x80DA25, 0x8025DA};
    for (uint32_t page = 0; page < Grid2D::PageCount; ++page)
        for (uint32_t x = 0; x < directions.size(); ++x)
            world.SetCell(x, page * Grid2D::PageHeight, {x % 2 ? pixel::CellTypes::Dirt : pixel::CellTypes::Empty, directions[x]});
    WorldGridFrame output;
    auto *storage = output.Cells().data();
    output.Post.Values[1] = 0.35f;
    BuildForceFieldFrame(world, output);
    Check(output.DisplayMode == GridDisplayMode::ForceField && output.Post.Values == PostProcessParameters{}.Values, "debug frame retained material mode or tint");
    ValidateWorldGridFrame(output.Read(), 2, 800, 600);
    for (uint32_t page = 0; page < Grid2D::PageCount; ++page)
        for (size_t x = 0; x < directions.size(); ++x) {
            Check(output.Cells()[size_t(page * Grid2D::PageHeight) * Grid2D::Width + x] == colors[x], "packed field colours or page indexing incorrect");
            Check(world.GetCell(uint32_t(x), page * Grid2D::PageHeight) == pixel::CellState{x % 2 ? pixel::CellTypes::Dirt : pixel::CellTypes::Empty, directions[x]}, "debug publication modified the world");
        }
    Check(output.Cells().back() == 0x808080, "empty zero-force cell did not display grey");
    world.SetForceDirection(1, 0, {-1, 0});
    Check(output.Cells()[1] == 0x8080FF, "unpublished force edit changed the debug snapshot");
    BuildForceFieldFrame(world, output);
    Check(output.Cells()[1] == 0x808000 && output.Cells().data() == storage, "next publication missed an edit or reallocated storage");

    pixel::WindSettings wind;
    wind.Ambient = {60, 0};
    wind.GustSpeed = 0;
    world.ConfigureWind(424, wind);
    BuildForceFieldFrame(world, output);
    Check(output.Cells()[1] == 0x808080, "wind cancellation was not displayed as zero force");
    Check(output.Cells()[100 * Grid2D::Width + 100] == 0x8080FF && output.Cells()[424 * Grid2D::Width + 100] == 0x808080, "wind region or air-cell colours are incorrect");
    world.DisableWind();
    Check(output.Cells()[100 * Grid2D::Width + 100] == 0x8080FF, "wind edit mutated a published frame");
    BuildWorldGridFrame(world.ReadState(), {{pixel::CellTypes::Dirt.Value, 1}}, output);
    Check(output.DisplayMode == GridDisplayMode::Materials && output.Cells()[1] == 1 && output.Cells().back() == 0, "material view retained encoded debug colours");
    ValidateWorldGridFrame(output.Read(), 2, 800, 600);
    pixel::World small(4, 4);
    Reject<std::invalid_argument>([&] { BuildForceFieldFrame(small, output); });
}
} // namespace

void RunPixelFieldViewTests() {
    DirectionColors();
    CombinedFieldSampling();
    FieldFramePublication();
}
