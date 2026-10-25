add_subdirectory(Assets)
# CPU presentation consumes gameplay camera policy and committed world state.
add_library(PixelPresentation STATIC
    WorldGridFrame.cpp WindVisualFrame.cpp FormulaFrame.cpp DeviceFrame.cpp ForceGardenFrame.cpp PixelWorldPresentation.cpp GamePresentation.cpp BackgroundView.cpp)
target_link_libraries(PixelPresentation PUBLIC PixelGameplay PixelCamera PixelAssets Axiom::Core)

# Runtime supplies the real RHI backend; CPU tests supply mocks and shader lookup.
add_library(PixelRender STATIC
    PixelRender.cpp Passes/RenderResources.cpp Passes/UploadPass.cpp
    Passes/WorldPass.cpp Passes/WindPass.cpp Passes/SceneObjectsPass.cpp
    Passes/CharacterPass.cpp Passes/PostProcessPass.cpp)
target_link_libraries(PixelRender PUBLIC PixelPresentation)
