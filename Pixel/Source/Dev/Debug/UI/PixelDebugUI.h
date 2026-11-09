#pragma once
#include "Runtime/System/IO/Input/GameInput.h"
#include "FormulaDebugUI.h"
#include "ForceGardenUI.h"
#include <optional>

namespace pixel {
class GameSession;
class GameCamera;
class GamePresentation;
namespace performance { class GameProfiler; }
namespace will { class WorldWill; }

// Owns panel visibility and style; simulation and presentation remain authoritative.
class PixelDebugUI {
  public:
    void Toggle() { Visible = !Visible; }
    void ToggleFormulaEditor() { FormulaEditor.Visible = !FormulaEditor.Visible; }
    void ApplyUIScale(float scale);
    // Mode changes return to GameInput through the application's lifecycle adapter.
    std::optional<ControlMode> Draw(GameSession &session, GameCamera &camera,
                                    GamePresentation &presentation, GameInput &input,
                                    performance::GameProfiler &performance, will::WorldWill &worldWill, ui::InventoryUI &ui);
  private:
    bool Visible = true;
    FormulaDebugUI FormulaEditor;
    std::array<int64_t, 2> ActionCell{520, 512};
    std::array<int64_t, 2> DeviceCell{520, 510};
    uint64_t SelectedDevice = 0;
    std::optional<api::RequestKey> PendingDevicePlacement;
    ForceGardenUIState GardenUI;
    std::function<void(float)> ScaleStyle;
    float AppliedUIScale = 0;
};
} // namespace pixel
