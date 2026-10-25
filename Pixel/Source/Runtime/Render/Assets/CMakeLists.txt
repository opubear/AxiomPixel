add_library(PixelAssets STATIC ArtAssets.cpp)
target_link_libraries(PixelAssets PUBLIC Axiom::Core)
# Use the engine's locked header dependencies in CPU and runtime builds alike.
target_include_directories(PixelAssets PRIVATE
    "${AXIOM_ENGINE_DIR}/ThirdParty/nlohmannJson/single_include"
    "${AXIOM_ENGINE_DIR}/ThirdParty/stb")
target_include_directories(PixelAssets PUBLIC "${PIXEL_SOURCE_DIR}")
