#include "Performance/FrameProfiler.h"
#include "WorldGridFrame.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

std::optional<std::array<int64_t, 2>> PickGridCell(const GridView2D &view,
                                                   std::array<double, 2> cursor,
                                                   std::array<int, 2> windowSize,
                                                   std::array<int, 2> framebufferSize) {
    std::array<int64_t, 2> cell{};
    for (size_t axis = 0; axis < 2; ++axis) {
        if (windowSize[axis] <= 0 || framebufferSize[axis] <= 0 || !view.CellSizePixels[axis] || !std::isfinite(cursor[axis]) || cursor[axis] < 0 || cursor[axis] >= windowSize[axis])
            return std::nullopt;
        const double pixel = std::floor(cursor[axis] * framebufferSize[axis] / windowSize[axis]) + view.OriginPixels[axis];
        const double local = std::floor(pixel / view.CellSizePixels[axis]);
        if (local < double(INT64_MIN) || local >= double(INT64_MAX))
            return std::nullopt;
        const int64_t offset = int64_t(local);
        if ((offset > 0 && view.WorldOriginCells[axis] > INT64_MAX - offset) || (offset < 0 && view.WorldOriginCells[axis] < INT64_MIN - offset))
            return std::nullopt;
        cell[axis] = view.WorldOriginCells[axis] + offset;
    }
    return cell;
}

void WorldGridFrame::SetRegion(std::array<int64_t, 2> origin, uint32_t width, uint32_t height) {
    if (!width || !height || size_t(width) > std::numeric_limits<size_t>::max() / height / sizeof(uint32_t))
        throw std::invalid_argument("render region requires representable positive dimensions");
    if (origin == CellOrigin && width == Width && height == Height)
        return;

    const bool shapeChanged = width != Width || height != Height;
    Width = width;
    Height = height;
    CellOrigin = origin;
    if (shapeChanged) {
        std::vector<uint32_t>(size_t(width) * height, Grid2D::MissingCell).swap(CellStorage);
        std::vector<uint32_t>(size_t(width) * height, WindSolidTexel).swap(WindStorage);
    } else {
        std::fill(CellStorage.begin(), CellStorage.end(), Grid2D::MissingCell);
        std::fill(WindStorage.begin(), WindStorage.end(), WindSolidTexel);
    }
    SourceVersions.clear();
    Versions.clear();
    WindVersions.clear();
    WindSourceVersions.clear();
    WindRegionActive.clear();
}

bool WorldGridFrame::PrepareRegionVersions(uint64_t signature) {
    const size_t count = size_t(RegionColumns()) * RegionRows();
    const bool changed = Versions.size() != count || signature != AppearanceSignature;
    if (changed) {
        SourceVersions.assign(count, UINT64_MAX);
        if (Versions.size() != count)
            Versions.assign(count, 0);
        AppearanceSignature = signature;
    }
    return changed;
}

void WorldGridFrame::SetSourceRegionVersion(size_t index, uint64_t version, bool contentsChanged) {
    SourceVersions.at(index) = version;
    if (contentsChanged)
        Versions.at(index) = NextVersion++;
}

void WorldGridFrame::MarkWindRegionChanged(size_t index) {
    const size_t count = size_t(RegionColumns()) * RegionRows();
    if (WindVersions.size() != count)
        WindVersions.assign(count, 0);
    if (WindRegionActive.size() != count)
        WindRegionActive.assign(count, 0);
    WindVersions.at(index) = NextVersion++;
}

bool WorldGridFrame::PrepareWindRegionVersions(uint64_t revision) {
    const size_t count = size_t(RegionColumns()) * RegionRows();
    const bool changed = WindSourceVersions.size() != count || WindRevision != revision;
    if (changed) {
        WindSourceVersions.assign(count, UINT64_MAX);
        WindRevision = revision;
    }
    if (WindVersions.size() != count)
        WindVersions.assign(count, 0);
    if (WindRegionActive.size() != count)
        WindRegionActive.assign(count, 0);
    return changed;
}

pixel::performance::MemoryUsage WorldGridFrame::GetMemoryUsage() const {
    return pixel::performance::MeasureObject(*this, CellStorage, WindStorage, SourceVersions, Versions, WindVersions, WindSourceVersions, WindRegionActive);
}

void ValidateGridTextureCatalog(const GridTextureCatalog &catalog) {
    const auto maximum = std::numeric_limits<size_t>::max();
    if (!catalog.Width || !catalog.Height || !catalog.Layers || !catalog.CellsPerTile || catalog.CellsPerTile > 4096 || size_t(catalog.Width) > maximum / 4 / catalog.Height / catalog.Layers || catalog.Pixels.size() != size_t(catalog.Width) * catalog.Height * catalog.Layers * 4)
        throw std::invalid_argument("texture catalog requires equal-sized, tightly packed RGBA8 layers");
}
std::array<int32_t, 2> LocalGridPixelOrigin(const WorldGridFrameView &frame) {
    std::array<int32_t, 2> origin{};
    for (size_t axis = 0; axis < 2; ++axis) {
        const int64_t a = frame.View.WorldOriginCells[axis], b = frame.CellOrigin[axis];
        if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b))
            throw std::invalid_argument("render region is too far from the camera reference");
        const int64_t difference = a - b;
        const double pixels = frame.View.OriginPixels[axis] + double(difference) * frame.View.CellSizePixels[axis];
        if (pixels < INT32_MIN || pixels > INT32_MAX)
            throw std::invalid_argument("render region is too far from the camera reference");
        origin[axis] = int32_t(pixels);
    }
    return origin;
}
void ValidateWorldGridFrame(const WorldGridFrameView &frame, uint32_t layers, uint32_t width, uint32_t height) {
    PIXEL_PROFILE_SCOPE(Validation);
    if (!frame.Width || !frame.Height || frame.Cells.size() != size_t(frame.Width) * frame.Height || !layers || !width || !height)
        throw std::invalid_argument("grid frame requires a complete region and a drawable viewport");
    const auto localOrigin = LocalGridPixelOrigin(frame);
    const size_t regionCount = size_t((frame.Width - 1) / Grid2D::RegionSize + 1) * ((frame.Height - 1) / Grid2D::RegionSize + 1);
    if ((!frame.RegionVersions.empty() && frame.RegionVersions.size() != regionCount) || (!frame.WindRegionVersions.empty() && frame.WindRegionVersions.size() != regionCount))
        throw std::invalid_argument("render region version count differs from grid dimensions");
    constexpr auto maximum = std::numeric_limits<int32_t>::max();
    const std::array<uint32_t, 2> world{frame.Width, frame.Height};
    const std::array<uint32_t, 2> extent{width, height};
    for (size_t axis = 0; axis < 2; ++axis) {
        const auto size = frame.View.CellSizePixels[axis];
        if (!size || uint64_t(size) * world[axis] > maximum || extent[axis] > uint32_t(maximum) || int64_t(localOrigin[axis]) + extent[axis] - 1 > maximum)
            throw std::invalid_argument("grid view dimensions or origin exceed integer pixel coordinates");
    }
    if (frame.View.Background[3] != 1 || std::any_of(frame.View.Background.begin(), frame.View.Background.end(), [](float v) { return !std::isfinite(v) || v < 0 || v > 1; }) || std::any_of(frame.Post.Values.begin(), frame.Post.Values.end(), [](float v) { return !std::isfinite(v); }))
        throw std::invalid_argument("grid background must be opaque and rendering parameters finite");
    if (uint32_t(frame.Wind.Mode) > uint32_t(WindVisualMode::AirRefraction) || !std::isfinite(frame.Wind.Opacity) || frame.Wind.Opacity < 0 || frame.Wind.Opacity > 1 || !std::isfinite(frame.Wind.RefractionPixels) || frame.Wind.RefractionPixels < 0 || frame.Wind.RefractionPixels > 12 || !std::isfinite(frame.Wind.MotionScale) || frame.Wind.MotionScale < 0 || frame.Wind.MotionScale > 2 || !std::isfinite(frame.WindTimeSeconds) || frame.WindTimeSeconds < 0 || frame.WindTimeSeconds >= 4096 || (frame.HasWind && frame.WindCells.size() != frame.Cells.size()))
        throw std::invalid_argument("invalid wind visual snapshot or parameters");
    if (frame.Character) {
        const auto &character = *frame.Character;
        const auto finite = [](const auto &values) {
            return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
        };
        const auto validColor = [&](const auto &color) {
            return finite(color) && color[3] == 1 && std::all_of(color.begin(), color.end(), [](float value) { return value >= 0 && value <= 1; });
        };
        if (!finite(character.Bounds) || character.Bounds[2] <= 0 || character.Bounds[3] <= 0 ||
            !std::isfinite(character.Bounds[0] + character.Bounds[2]) || !std::isfinite(character.Bounds[1] + character.Bounds[3]) ||
            !validColor(character.HeadColor) || !validColor(character.BodyColor) || !finite(character.Parameters) ||
            character.Parameters[0] < 0 || character.Parameters[0] > 1)
            throw std::invalid_argument("character geometry and opaque colours must be finite and valid");
    }
    switch (frame.DisplayMode) {
    case GridDisplayMode::Materials:
        if (std::any_of(frame.Cells.begin(), frame.Cells.end(), [layers](uint32_t id) {
                const auto layer = id & Grid2D::MaterialLayerMask;
                return id != Grid2D::MissingCell && (layer >= layers || ((id & Grid2D::SurfaceCellFlag) && !layer) ||
                       ((id & Grid2D::SurfaceStripMask) && !(id & Grid2D::SurfaceCellFlag)));
            }))
            throw std::invalid_argument("grid TextureId exceeds the texture catalog");
        break;
    case GridDisplayMode::ForceField:
        if (std::any_of(frame.Cells.begin(), frame.Cells.end(), [](uint32_t color) { return color != Grid2D::MissingCell && color > 0xFFFFFF; }))
            throw std::invalid_argument("force-field color must be packed RGB8");
        break;
    default:
        throw std::invalid_argument("unknown grid display mode");
    }
}
