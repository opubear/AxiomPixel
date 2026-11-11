#pragma once
#include "Canvas.h"
#include <array>

namespace pixel::ui {
struct GameMenuLayout {
    static constexpr int Width = 300, Height = 196;
    static Rect ReturnButton() { return {20, 60, 260, 42}; }
    static Rect QuitButton() { return {20, 112, 260, 42}; }
    static Rect Placement(std::array<int, 2> framebuffer, float uiScale = 1);
};

enum class GameMenuAction { None,
                            Return,
                            Quit };

class GameMenuUI {
  public:
    bool IsOpen() const { return Open; }
    void Toggle() { Open = !Open; }
    void SetExitBlocked(bool blocked) { ExitBlocked = blocked; }
    float GetUIScale() const { return Image.UIScale; }
    void SetUIScale(float scale);
    GameMenuAction PointerPress(float x, float y, std::array<int, 2> framebuffer, bool primary) const;
    const Canvas &Paint();

  private:
    bool Open = false;
    bool ExitBlocked = false;
    bool PaintedExitBlocked = false;
    Canvas Image;
};
} // namespace pixel::ui
