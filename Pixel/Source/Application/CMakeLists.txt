add_library(PixelGame STATIC PixelGame.cpp PixelRuntime.cpp PixelApplication.cpp)
target_link_libraries(PixelGame PUBLIC PixelRender PixelDebugUI PixelLLMView AxiomApplication PRIVATE spdlog)
add_executable(Pixel "${AXIOM_ENGINE_DIR}/Source/main.cpp" EntryPoint.cpp)
target_link_libraries(Pixel PRIVATE PixelGame spdlog)
axiom_copy_runtime_dlls(Pixel)
set_target_properties(Pixel PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/Pixel")
add_custom_target(run-pixel
    COMMAND "$<TARGET_FILE:Pixel>"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/Pixel"
    DEPENDS Pixel USES_TERMINAL)
