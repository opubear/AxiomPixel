#pragma once
#include "Formula/Process/Editing.h"
#include <array>
#include <string>

namespace pixel {
class GameSession;
// Owns the temporary process source draft; published programs live in FormulaBridge.
class FormulaDebugUI {
  public:
    bool Visible = true;
    void Draw(GameSession &session);

  private:
    std::string Status;
    bool Initialized = false;
    formula::process::editing::Draft ProcessDraft{"editor.process"};
    std::array<char, 16385> ProcessText{};
    void DrawProcessExample(GameSession &session);
    void DrawProcessEditor(GameSession &session);
    void DrawDiagnostics(GameSession &session);
};
} // namespace pixel
