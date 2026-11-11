#include "BackgroundView.h"
#include <cmath>

namespace pixel {
float BackgroundPhase(const BackgroundLayer &layer, const GridView2D &view, uint32_t framebufferWidth) {
    const double localCentre = (double(view.OriginPixels[0]) + framebufferWidth / 2.0) / view.CellSizePixels[0];
    const int64_t period = layer.RepeatCells;
    // Reduce before converting to double, including negative and distant cells.
    double phase = std::fmod(double(view.WorldOriginCells[0] % period) + localCentre, double(period));
    if (phase < 0) phase += period;
    return float(2 * phase / period);
}

std::array<float, 4> BackgroundPhases(const BackgroundAssets &assets, const GridView2D &view,
                                    uint32_t framebufferWidth) {
    std::array<float, 4> phases{};
    for (size_t index = 0; index < assets.Layers.size(); ++index)
        phases[index] = BackgroundPhase(assets.Layers[index], view, framebufferWidth);
    return phases;
}
} // namespace pixel
