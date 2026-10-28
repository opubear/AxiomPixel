#pragma once
#include "Runtime/Gameplay/Camera/GridView.h"

#include "Dev/Debug/Performance/MemoryUsage.h"
#include "WindVisualFrame.h"
#include "Runtime/Simulation/World/WorldStorageConfig.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace Grid2D {
inline constexpr uint32_t RegionSize = pixel::storage::RegionEdge;
inline constexpr uint32_t WindHalo = 32;
inline constexpr uint32_t MissingCell = UINT32_MAX;
// Material frames may request the exposed top strip of a surface image.
// Force-field packed colours and MissingCell are interpreted before this flag.
inline constexpr uint32_t SurfaceCellFlag = 0x80000000u;
inline constexpr uint32_t SurfaceStripShift = 28;
inline constexpr uint32_t SurfaceStripMask = 0x70000000u;
inline constexpr uint32_t MaterialLayerMask = 0x0fffffffu;
inline constexpr uint32_t Width = 1024;
inline constexpr uint32_t Height = 1024;
inline constexpr uint32_t PageHeight = 256;
inline constexpr uint32_t PageCount = Height / PageHeight;
inline constexpr size_t CellCount = size_t(Width) * Height;
inline constexpr size_t UploadBytes = CellCount * sizeof(uint32_t);
inline constexpr size_t PageBytes = UploadBytes / PageCount;
} // namespace Grid2D
struct PostProcessParameters {
    std::array<float, 16> Values{1, 1, 1, 1};
};
enum class GridDisplayMode : uint32_t {
    Materials = 0,
    ForceField = 1,
};
// Render-owned character snapshot. Bounds use continuous framebuffer pixels.
struct CharacterDraw {
    std::array<float, 4> Bounds{}; // left, top, width, height
    std::array<float, 4> HeadColor{}; // linear RGBA
    std::array<float, 4> BodyColor{};
    std::array<float, 4> Parameters{0.25f, 0, 0, 0}; // head height fraction
    std::array<float, 4> FrameTexels{}; // atlas x/y, frame width/height
    std::array<float, 4> Sprite{}; // layer count (zero = diagnostic rectangle), mirror X
};
static_assert(sizeof(CharacterDraw) == 96);
// Opaque render-owned projectile geometry. IDs remain CPU diagnostics only.
struct FormulaDraw {
    std::array<float, 4> CenterRadius{}; // centre X/Y, horizontal/vertical radius
    std::array<float, 4> Color{1, 1, 0, 1}; // linear opaque yellow
    uint64_t WorldEpoch = 0, CastId = 0, ProjectileId = 0;
};
// Render-owned device rectangles. Gameplay identity is diagnostic only.
struct DeviceDraw {
    std::array<float, 4> CenterRadius{};
    std::array<float, 4> Color{};
    std::array<float, 4> OutputCenterRadius{};
    std::array<float, 4> OutputColor{1, 1, 1, 1};
    uint64_t DeviceId = 0;
};
enum class GardenObject { Crystal, WildPlant, HomePlant };
// Garden geometry shares the opaque scene compositor, not projectile identity.
struct GardenDraw {
    std::array<float, 4> CenterRadius{};
    std::array<float, 4> Color{};
    GardenObject Object = GardenObject::Crystal;
    bool Ellipse = false;
};
// Opaque parts of a non-player actor, converted from committed gameplay values.
struct ActorDraw {
    std::array<float, 4> CenterRadius{};
    std::array<float, 4> Color{};
    uint64_t ActorId = 0;
};
struct WorldGridFrameView {
    // Material layer IDs plus surface/strip bits, or packed linear RGB8 in ForceField mode.
    std::span<const uint32_t> Cells;
    GridView2D View;
    PostProcessParameters Post;
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;
    std::optional<CharacterDraw> Character;
    WindVisualSettings Wind;
    float WindTimeSeconds = 0;
    bool HasWind = false;
    std::span<const uint32_t> WindCells;
    uint32_t Width = Grid2D::Width, Height = Grid2D::Height;
    std::array<int64_t, 2> CellOrigin{};
    std::span<const uint64_t> RegionVersions;
    std::span<const uint64_t> WindRegionVersions;
    bool MissingOutsideRegion = false;
    std::span<const FormulaDraw> FormulaDraws;
    std::span<const DeviceDraw> DeviceDraws;
    std::span<const GardenDraw> GardenDraws;
    std::span<const ActorDraw> ActorDraws;
};
// Persistent render-owned output, filled at FrameBegin. Gameplay storage is
// converted into this format; it is never reinterpreted as texture indices.
class WorldGridFrame {
  public:
    // Publication reserves its final region before growing these two planes.
    // Keep the unpublished fallback small enough for constrained world budgets.
    WorldGridFrame() : CellStorage(size_t(Grid2D::RegionSize) * Grid2D::RegionSize),
                       WindStorage(size_t(Grid2D::RegionSize) * Grid2D::RegionSize) {}
    ~WorldGridFrame() {
        if (ReleaseBudget)
            ReleaseBudget();
    }
    WorldGridFrame(const WorldGridFrame &) = delete;
    WorldGridFrame &operator=(const WorldGridFrame &) = delete;
    std::span<uint32_t> Cells() { return CellStorage; }
    std::span<uint32_t> WindCells() { return WindStorage; }
    WorldGridFrameView Read() const { return {CellStorage, View, Post, DisplayMode, Character, Wind, WindTimeSeconds, HasWind, WindStorage, Width, Height, CellOrigin, Versions, WindVersions, MissingOutsideRegion, FormulaDraws, DeviceDraws, GardenDraws, ActorDraws}; }
    void SetRegion(std::array<int64_t, 2> origin, uint32_t width, uint32_t height);
    uint32_t GetWidth() const { return Width; }
    uint32_t GetHeight() const { return Height; }
    std::array<int64_t, 2> GetCellOrigin() const { return CellOrigin; }
    uint32_t RegionColumns() const { return (Width - 1) / Grid2D::RegionSize + 1; }
    uint32_t RegionRows() const { return (Height - 1) / Grid2D::RegionSize + 1; }
    // The appearance signature includes the display mode; a changed mapping
    // invalidates every source region without changing gameplay versions.
    bool PrepareRegionVersions(uint64_t appearanceSignature);
    uint64_t SourceRegionVersion(size_t index) const { return SourceVersions.at(index); }
    void SetSourceRegionVersion(size_t index, uint64_t version, bool contentsChanged = true);
    void MarkWindRegionChanged(size_t index);
    bool PrepareWindRegionVersions(uint64_t revision);
    uint64_t WindSourceRegionVersion(size_t index) const { return WindSourceVersions.at(index); }
    void SetWindSourceRegionVersion(size_t index, uint64_t revision) { WindSourceVersions.at(index) = revision; }
    bool WindRegionIsActive(size_t index) const { return WindRegionActive.at(index) != 0; }
    void SetWindRegionActive(size_t index, bool active) { WindRegionActive.at(index) = active; }
    pixel::performance::MemoryUsage GetMemoryUsage() const;
    // Entity geometry is reported separately from world publication planes.
    pixel::performance::MemoryUsage GetFormulaMemoryUsage() const {
        return pixel::performance::VectorStorage(FormulaDraws);
    }
    pixel::performance::MemoryUsage GetDeviceMemoryUsage() const {
        return pixel::performance::VectorStorage(DeviceDraws);
    }
    void TrackPublication(uint64_t identity, std::function<void()> release) {
        if (BudgetIdentity == identity)
            return;
        if (ReleaseBudget)
            ReleaseBudget();
        BudgetIdentity = identity;
        ReleaseBudget = std::move(release);
    }
    GridView2D View;
    PostProcessParameters Post;
    GridDisplayMode DisplayMode = GridDisplayMode::Materials;
    std::optional<CharacterDraw> Character;
    WindVisualSettings Wind;
    float WindTimeSeconds = 0;
    bool HasWind = false;
    bool MissingOutsideRegion = false;
    std::vector<FormulaDraw> FormulaDraws;
    std::vector<DeviceDraw> DeviceDraws;
    std::vector<GardenDraw> GardenDraws;
    std::vector<ActorDraw> ActorDraws;

  private:
    std::vector<uint32_t> CellStorage, WindStorage;
    uint32_t Width = Grid2D::RegionSize, Height = Grid2D::RegionSize;
    std::array<int64_t, 2> CellOrigin{};
    std::vector<uint64_t> SourceVersions, Versions, WindVersions, WindSourceVersions;
    std::vector<uint8_t> WindRegionActive;
    uint64_t AppearanceSignature = 0, NextVersion = 1;
    uint64_t WindRevision = 0;
    uint64_t BudgetIdentity = 0;
    std::function<void()> ReleaseBudget;
};
// Layer zero is reserved for empty cells. Pixels contain tightly packed RGBA8,
// in layer-major order; all layers share the same dimensions and sRGB encoding.
struct GridTextureCatalog {
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Layers = 0;
    std::vector<std::byte> Pixels;
    uint32_t CellsPerTile = 1; // one complete body texture per N world cells, in [1,4096]
};
void ValidateGridTextureCatalog(const GridTextureCatalog &catalog);
std::array<int32_t, 2> LocalGridPixelOrigin(const WorldGridFrameView &frame);
void ValidateWorldGridFrame(const WorldGridFrameView &frame, uint32_t textureLayers,
                            uint32_t framebufferWidth, uint32_t framebufferHeight);
