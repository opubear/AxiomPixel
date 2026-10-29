set(PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL "4" CACHE STRING "Framebuffer pixels per side of one logical world pixel (1..64)")
set_property(CACHE PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL PROPERTY STRINGS 1 2 4 8 16 32 64)
if(NOT PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL MATCHES "^[1-9][0-9]*$" OR
   PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL GREATER 64)
    message(FATAL_ERROR "PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL must be an integer in [1, 64]")
endif()

add_library(PixelCamera STATIC Camera2D.cpp GameCamera.cpp GridView.cpp)
target_include_directories(PixelCamera PUBLIC "${PIXEL_SOURCE_DIR}" "${AXIOM_ENGINE_DIR}/Source")
target_link_libraries(PixelCamera PUBLIC PixelActors)
# Every consumer of GridView2D must see the same default, including Input,
# Presentation and runtime tests. Do not leak a display setting into Simulation.
target_compile_definitions(PixelCamera PUBLIC
    PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL=${PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL})
