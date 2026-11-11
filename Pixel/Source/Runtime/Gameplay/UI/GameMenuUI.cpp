#include "GameMenuUI.h"
#include "CanvasPainter.h"
#include "UIScale.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixel::ui {
Rect GameMenuLayout::Placement(std::array<int, 2> framebuffer, float uiScale) {
    if (!IsValidUIScale(uiScale))
        throw std::invalid_argument("UIScale must be finite and between 0.25 and 4");
    if (framebuffer[0] <= 0 || framebuffer[1] <= 0)
        return {};
    const float base = std::min({2.f, float(framebuffer[0]) / 1280, float(framebuffer[1]) / 720});
    const float scale = std::min({base * uiScale, float(framebuffer[0]) / (Width + 32),
                                  float(framebuffer[1]) / (Height + 32)});
    return {(framebuffer[0] - Width * scale) / 2, (framebuffer[1] - Height * scale) / 2,
            Width * scale, Height * scale};
}

void GameMenuUI::SetUIScale(float scale) {
    if (!IsValidUIScale(scale))
        throw std::invalid_argument("UIScale must be finite and between 0.25 and 4");
    Image.UIScale = scale;
}

GameMenuAction GameMenuUI::PointerPress(float x, float y, std::array<int, 2> framebuffer, bool primary) const {
    if (!Open || !primary || !std::isfinite(x) || !std::isfinite(y))
        return GameMenuAction::None;
    const auto bounds = GameMenuLayout::Placement(framebuffer, GetUIScale());
    if (!bounds.Width || !bounds.Contains(x, y))
        return GameMenuAction::None;
    x = (x - bounds.X) * GameMenuLayout::Width / bounds.Width;
    y = (y - bounds.Y) * GameMenuLayout::Height / bounds.Height;
    if (GameMenuLayout::ReturnButton().Contains(x, y))
        return GameMenuAction::Return;
    if (GameMenuLayout::QuitButton().Contains(x, y))
        return GameMenuAction::Quit;
    return GameMenuAction::None;
}

const Canvas &GameMenuUI::Paint() {
    Image.Visible = Open;
    if (Image.Revision && PaintedExitBlocked == ExitBlocked)
        return Image;
    using namespace palette;
    CanvasPainter paint(Image.Pixels, GameMenuLayout::Width, GameMenuLayout::Height);
    paint.Box(0, 0, GameMenuLayout::Width, GameMenuLayout::Height, Border, Panel);
    paint.Fill(0, 0, GameMenuLayout::Width, 3, Gold);
    paint.Text(96, 22, "GAME MENU", TextColor, 2);
    const auto button = [&](Rect rect, std::string_view text, Color color) {
        paint.Box(int(rect.X), int(rect.Y), int(rect.Width), int(rect.Height), Border, Inset);
        paint.Text(int(rect.X + (rect.Width - text.size() * 12) / 2), int(rect.Y + 14), text, color, 2);
    };
    button(GameMenuLayout::ReturnButton(), "RETURN TO GAME", TextColor);
    button(GameMenuLayout::QuitButton(), "QUIT GAME", Gold);
    if (ExitBlocked)
        paint.Text(81, 166, "EXIT BLOCKED - TRY AGAIN", Gold);
    paint.Text(111, 180, "ESC TO RETURN", Muted);
    PaintedExitBlocked = ExitBlocked;
    ++Image.Revision;
    return Image;
}
} // namespace pixel::ui
