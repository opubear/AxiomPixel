#pragma once
#include "FormulaEditing/FormulaDraft.h"
#include "Formula/Process/Editing.h"
#include <array>
#include <string>

namespace pixel {
class GameSession;
// Owns the mutable draft and selection; active descriptions live in FormulaBridge.
class FormulaDebugUI {
  public:
    bool Visible = true;
    void Draw(GameSession &session);

  private:
    formula::editing::Draft Draft;
    formula::editing::CompileResult Compiled;
    formula::editing::NodeId Selected = 0;
    std::string Preview, Status;
    bool Initialized = false, Dirty = false;
    formula::process::editing::Draft ProcessDraft{"editor.process"};
    std::array<char, 16385> ProcessText{};
    int NewKind = int(formula::NodeKind::Bolt);
    void Rebuild();
    void LoadApplied(GameSession &session);
    void DrawToolbar(GameSession &session);
    void DrawProcessExample(GameSession &session);
    void DrawProcessEditor(GameSession &session);
    void DrawTree(const formula::editing::DraftNode &node, const char *slot = "");
    void DrawProperties();
    void DrawDiagnostics(GameSession &session);
    void SelectOperation(formula::editing::NodeId selection);
};
} // namespace pixel
