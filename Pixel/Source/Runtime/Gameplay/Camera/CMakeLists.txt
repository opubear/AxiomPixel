add_library(PixelCamera STATIC Camera2D.cpp GameCamera.cpp GridView.cpp)
target_include_directories(PixelCamera PUBLIC "${PIXEL_SOURCE_DIR}" "${AXIOM_ENGINE_DIR}/Source")
target_link_libraries(PixelCamera PUBLIC PixelActors)
