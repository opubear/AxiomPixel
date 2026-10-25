#include "BackgroundView.h"
#include <cmath>

namespace pixel {
std::array<float, 4> BackgroundPhases(const BackgroundAssets &assets, const GridView2D &view,
                                    uint32_t framebufferWidth) {
    std::array<float, 4> phases{};
    const double localCentre = (double(view.OriginPixels[0]) + framebufferWidth / 2.0) / view.CellSizePixels[0];

    for (size_t index = 0; index < assets.Layers.size(); ++index) {
        const int64_t period = assets.Layers[index].RepeatCells;
        // Reduce the integer reference before converting it to double. This
        // preserves motion across chunk boundaries even beyond double precision.
        double phase = std::fmod(double(view.WorldOriginCells[0] % period) + localCentre, double(period));
        if (phase < 0)
            phase += period;
        phases[index] = float(2 * phase / period);
    }
    return phases;
}
} // namespace pixel
