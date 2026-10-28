# CPU input invokes gameplay and an injected presentation-control port.
add_library(PixelInput STATIC GameInput.cpp)
target_include_directories(PixelInput PUBLIC
    "${PIXEL_SOURCE_DIR}"
    "${AXIOM_ENGINE_DIR}/Source"
    "${AXIOM_ENGINE_DIR}/ThirdParty/glfw/include")
# SystemEnum exposes GLFW key constants, but input has no OpenGL dependency.
target_compile_definitions(PixelInput PUBLIC GLFW_INCLUDE_NONE)
target_compile_features(PixelInput PUBLIC cxx_std_20)
target_link_libraries(PixelInput PUBLIC PixelGameplay PixelCamera PixelUI)
