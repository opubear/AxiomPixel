#include "PixelWorldPresentation.h"
#include "Dev/Debug/Performance/FrameProfiler.h"
#include "Runtime/Gameplay/CommittedWorldView.h"
#include "Runtime/Simulation/World/World.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void ValidatePresentationWorld(pixel::WorldMapView world) {
    if (!world.IsValid() || !world.Width || !world.Height)
        throw std::invalid_argument("2D presentation requires a valid nonempty map region");
}
} // namespace
uint64_t GridSourceRegionVersion(pixel::WorldMapView world, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    // Compatibility views can start inside a quadtree leaf. A render tile can
    // then overlap four leaves, all of whose versions affect its contents.
    uint64_t version = 1469598103934665603ULL;
    for (const uint32_t oy : {0u, height - 1})
        for (const uint32_t ox : {0u, width - 1})
            version = (version ^ world.RegionVersion(world.Origin.X + x + ox, world.Origin.Y + y + oy)) * 1099511628211ULL;
    return version;
}
void BuildWorldGridFrame(pixel::WorldMapView world, const CellAppearanceTable &appearances, WorldGridFrame &output) {
    PIXEL_PROFILE_SCOPE(Materials);
    ValidatePresentationWorld(world);
    output.SetRegion({world.Origin.X, world.Origin.Y}, world.Width, world.Height);
    output.Character.reset();
    output.HasWind = false;
    output.DisplayMode = GridDisplayMode::Materials;
    // Order-independent signature: callers can edit the public appearance table.
    uint64_t signature = 0x6d6174657269616cULL ^ world.Identity() * 0xd6e8feb86659fd93ULL;
    bool hasSurfaces = false;
    for (const auto &[type, appearance] : appearances) {
        if (appearance.Layer > Grid2D::MaterialLayerMask || appearance.SurfaceLayer > Grid2D::MaterialLayerMask || appearance.SurfaceStrip > 7 || (!appearance.SurfaceLayer && appearance.SurfaceStrip))
            throw std::invalid_argument("invalid material appearance layer or surface strip");
        signature ^= (uint64_t(type) << 32 | appearance.Layer) * 0x9e3779b97f4a7c15ULL + (uint64_t(appearance.SurfaceLayer) << 3 | appearance.SurfaceStrip) * 0xd6e8feb86659fd93ULL;
        hasSurfaces |= appearance.SurfaceLayer != 0;
    }
    const bool invalidate = output.PrepareRegionVersions(signature);
    const auto cells = output.Cells();
    for (uint32_t ry = 0; ry < output.RegionRows(); ++ry) {
        for (uint32_t rx = 0; rx < output.RegionColumns(); ++rx) {
            const size_t region = size_t(ry) * output.RegionColumns() + rx;
            const uint32_t beginX = rx * Grid2D::RegionSize, beginY = ry * Grid2D::RegionSize;
            const auto width = std::min(Grid2D::RegionSize, world.Width - beginX);
            uint64_t version = GridSourceRegionVersion(world, beginX, beginY, width,
                                                       std::min(Grid2D::RegionSize, world.Height - beginY));
            // Top-edge materials depend on the preceding published row. Fold
            // both corner region versions in, including unaligned view origins.
            if (hasSurfaces && beginY > 0)
                for (const uint32_t x : {beginX, beginX + width - 1})
                    version = (version ^ world.RegionVersion(world.Origin.X + x, world.Origin.Y + beginY - 1)) * 1099511628211ULL;
            if (!invalidate && output.SourceRegionVersion(region) == version)
                continue;
            bool changed = false;
            for (uint32_t y = beginY; y < std::min(world.Height, beginY + Grid2D::RegionSize); ++y) {
                for (uint32_t x = beginX; x < std::min(world.Width, beginX + Grid2D::RegionSize); ++x) {
                    const size_t index = size_t(y) * world.Width + x;
                    const auto cell = world.TryAtLocal(x, y);
                    uint32_t value = Grid2D::MissingCell;
                    if (cell) {
                        const auto type = static_cast<uint32_t>(cell->Type);
                        value = 0;
                        if (type) {
                            const auto appearance = appearances.find(type);
                            if (appearance == appearances.end())
                                throw std::invalid_argument("gameplay cell type has no appearance mapping");
                            value = appearance->second.Layer;
                            if (appearance->second.SurfaceLayer && y > 0) {
                                const auto above = world.TryAtLocal(x, y - 1);
                                if (above && above->Type == pixel::CellTypeId::Air)
                                    value = appearance->second.SurfaceLayer | Grid2D::SurfaceCellFlag | (appearance->second.SurfaceStrip << Grid2D::SurfaceStripShift);
                            }
                        }
                    }
                    changed |= cells[index] != value;
                    cells[index] = value;
                }
            }
            output.SetSourceRegionVersion(region, version, changed);
        }
    }
}

std::array<float, 3> FieldDirectionColor(pixel::WindVelocity velocity) {
    if (!std::isfinite(velocity.X) || !std::isfinite(velocity.Y))
        throw std::invalid_argument("field direction must be finite");
    const double length = std::hypot(double(velocity.X), double(velocity.Y));
    if (length == 0)
        return {0.5f, 0.5f, 0.5f};
    return {float(0.5 + 0.5 * velocity.X / length), float(0.5 + 0.5 * velocity.Y / length), 0.5f};
}

namespace {
pixel::WorldMapView ReadSource(const pixel::World &world, pixel::WorldRect bounds) { return world.GetMap().View(bounds); }
pixel::WorldMapView ReadSource(const pixel::CommittedWorldView &world, pixel::WorldRect) { return world.ReadState(); }
uint64_t WindRevision(const pixel::World &world) { return world.GetEnvironment().GetWindSimulation().GetRevision(); }
uint64_t WindRevision(const pixel::CommittedWorldView &world) { return world.WindRevision(); }
uint64_t FieldSourceRevision(const pixel::World &world) { return world.GetEnvironment().GetFieldSources().Revision(); }
uint64_t FieldSourceRevision(const pixel::CommittedWorldView &world) { return world.FieldSourceRevision(); }
template <class ReadWorld>
void BuildForceFieldFrameImpl(const ReadWorld &world, WorldGridFrame &output) {
    PIXEL_PROFILE_SCOPE(ForceColors);
    output.Character.reset();
    output.HasWind = false;
    auto cells = output.Cells();
    const auto origin = output.GetCellOrigin();
    const auto source = ReadSource(world, {origin[0], origin[1], output.GetWidth(), output.GetHeight()});
    const uint64_t windRevision = WindRevision(world);
    const bool invalidate = output.PrepareRegionVersions(0x666f726365ULL ^ FieldSourceRevision(world) * 0xa0761d6478bd642fULL ^ windRevision * 0x9e3779b97f4a7c15ULL ^ source.Identity() * 0xd6e8feb86659fd93ULL);
    for (uint32_t ry = 0; ry < output.RegionRows(); ++ry) {
        for (uint32_t rx = 0; rx < output.RegionColumns(); ++rx) {
            const size_t region = size_t(ry) * output.RegionColumns() + rx;
            const uint32_t beginX = rx * Grid2D::RegionSize, beginY = ry * Grid2D::RegionSize;
            const uint64_t version = GridSourceRegionVersion(source, beginX, beginY,
                                                             std::min(Grid2D::RegionSize, output.GetWidth() - beginX),
                                                             std::min(Grid2D::RegionSize, output.GetHeight() - beginY));
            if (!invalidate && output.SourceRegionVersion(region) == version)
                continue;
            bool changed = false;
            for (uint32_t y = beginY; y < std::min(output.GetHeight(), beginY + Grid2D::RegionSize); ++y) {
                for (uint32_t x = beginX; x < std::min(output.GetWidth(), beginX + Grid2D::RegionSize); ++x) {
                    const auto index = size_t(y) * output.GetWidth() + x;
                    if (!world.TryGetCell(origin[0] + x, origin[1] + y)) {
                        changed |= cells[index] != Grid2D::MissingCell;
                        cells[index] = Grid2D::MissingCell;
                        continue;
                    }
                    const auto color = FieldDirectionColor(world.GetFieldVelocity(origin[0] + x, origin[1] + y));
                    uint32_t packed = 0;
                    for (size_t channel = 0; channel < color.size(); ++channel)
                        packed |= uint32_t(std::lround(std::clamp(color[channel], 0.f, 1.f) * 255)) << (channel * 8);
                    changed |= cells[index] != packed;
                    cells[index] = packed;
                }
            }
            output.SetSourceRegionVersion(region, version, changed);
        }
    }
    output.DisplayMode = GridDisplayMode::ForceField;
    // Debug colours must not inherit gameplay tint; switching back restores it.
    output.Post = {};
}

} // namespace
void BuildForceFieldFrame(const pixel::World &world, WorldGridFrame &output) { BuildForceFieldFrameImpl(world, output); }
void BuildForceFieldFrame(const pixel::CommittedWorldView &world, WorldGridFrame &output) { BuildForceFieldFrameImpl(world, output); }

void BuildCharacterFrame(const pixel::ActorSnapshot &character, const pixel::Camera2D &camera,
                         std::array<int, 2> framebufferSize, WorldGridFrame &output) {
    output.Character.reset();
    if (output.DisplayMode != GridDisplayMode::Materials)
        return;
    auto linearColor = [](std::array<int, 3> rgb) {
        std::array<float, 4> color{0, 0, 0, 1};
        for (size_t channel = 0; channel < 3; ++channel) {
            const float value = rgb[channel] / 255.f;
            color[channel] = value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }
        return color;
    };
    const auto cameraPosition = camera.GetWorldPosition();
    const auto reference = cameraPosition.Cell();
    const auto player = character.GetWorldPosition().RelativeTo(reference);
    const auto centre = cameraPosition.RelativeTo(reference);
    CharacterDraw draw;
    draw.Bounds = {
        float((player[0] - centre[0]) * output.View.CellSizePixels[0] + framebufferSize[0] / 2.0),
        float((player[1] - centre[1]) * output.View.CellSizePixels[1] + framebufferSize[1] / 2.0),
        pixel::Body::Width * output.View.CellSizePixels[0],
        pixel::Body::Height * output.View.CellSizePixels[1]};
    draw.HeadColor = linearColor({255, 212, 140});
    draw.BodyColor = linearColor({40, 210, 180});
    output.Character = draw;
}

pixel::WorldRect VisibleWorldRegion(const GridView2D &view, std::array<int, 2> framebufferSize) {
    std::array<int64_t, 2> begin{};
    std::array<uint32_t, 2> size{};
    for (size_t axis = 0; axis < 2; ++axis) {
        if (!view.CellSizePixels[axis] || framebufferSize[axis] <= 0)
            throw std::invalid_argument("visible region requires a drawable viewport");
        const double low = std::floor(double(view.OriginPixels[axis]) / view.CellSizePixels[axis]) - Grid2D::WindHalo;
        const double high = std::ceil((double(view.OriginPixels[axis]) + framebufferSize[axis]) / view.CellSizePixels[axis]) + Grid2D::WindHalo;
        const int64_t alignedLow = int64_t(std::floor(low / Grid2D::RegionSize)) * Grid2D::RegionSize;
        const int64_t alignedHigh = int64_t(std::ceil(high / Grid2D::RegionSize)) * Grid2D::RegionSize;
        if ((alignedLow < 0 && view.WorldOriginCells[axis] < INT64_MIN - alignedLow) || (alignedHigh > 0 && view.WorldOriginCells[axis] > INT64_MAX - alignedHigh) || uint64_t(alignedHigh - alignedLow) > UINT32_MAX)
            throw std::invalid_argument("visible region exceeds world coordinates");
        begin[axis] = view.WorldOriginCells[axis] + alignedLow;
        size[axis] = uint32_t(alignedHigh - alignedLow);
    }
    return {begin[0], begin[1], size[0], size[1]};
}
