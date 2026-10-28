#include "FrameProfiler.h"
#include <array>

namespace pixel::performance {
const axiom::diagnostics::Schema &ProfileSchema() {
    static const auto schema = [] {
        auto result = axiom::diagnostics::EngineSchema();
        constexpr auto stages = std::to_array({"publish", "publish.state_copy", "publish.materials", "publish.force_colors", "publish.wind_visual", "publish.validate",
                                               "game.tick", "world.environment", "wind.advance", "world.material_step", "wind.feedback", "world.character",
                                               "render.cpu", "render.upload_write", "render.world_commands", "render.wind_commands", "render.character_commands", "render.post_ui_commands", "ui.build"});
        constexpr auto counters = std::to_array({"simulation_steps", "material_cells_scanned", "active_regions", "wind_visual_cells_scanned",
                                                 "world_upload_bytes", "wind_upload_bytes", "simulation_input_us", "framebuffer_pixels"});
        static_assert(stages.size() + axiom::diagnostics::EngineStageCount == StageCount);
        static_assert(counters.size() + axiom::diagnostics::EngineCounterCount == CounterCount);
        for (auto name : stages)
            result.AddStage(name);
        for (auto name : counters)
            result.AddCounter(name);
        for (size_t i = 0; i < MemoryComponentCount; ++i)
            result.AddMemory(std::string(Name(MemoryComponent(i))));
        return result;
    }();
    return schema;
}

std::string_view Name(Stage stage) { return ProfileSchema().Stages().at(size_t(stage)); }

std::string_view Name(Counter counter) { return ProfileSchema().Counters().at(size_t(counter)); }
} // namespace pixel::performance
