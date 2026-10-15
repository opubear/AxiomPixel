add_library(PixelGame STATIC PixelGame.cpp PixelRuntime.cpp)
target_link_libraries(PixelGame PUBLIC PixelRender PixelDebugUI AxiomRuntime PRIVATE spdlog)
add_executable(Pixel main.cpp)
target_link_libraries(Pixel PRIVATE PixelGame)
set_target_properties(Pixel PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/Pixel")
add_custom_target(run-pixel
    COMMAND "$<TARGET_FILE:Pixel>"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/Pixel"
    DEPENDS Pixel USES_TERMINAL)
